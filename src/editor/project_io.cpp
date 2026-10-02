#include "marrow/editor/project.hpp"
#include "project_json.hpp"
#include "project_internal.hpp"
#include "atomic_file_write.hpp"
#include "atlas_packer.hpp"
#include "marrow/allocator.hpp"
#include <fstream>
#include <system_error>
#include <utility>

namespace marrow::editor {
using namespace project_detail;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

namespace {

using marrow::runtime::AtlasLoader;

bool ensure_output_directory(
    const std::filesystem::path& path,
    ProjectExportError* error_out) {
    const std::filesystem::path parent_path = path.parent_path();
    if (parent_path.empty()) {
        return true;
    }

    std::error_code error;
    std::filesystem::create_directories(parent_path, error);
    if (!error) {
        return true;
    }

    error_out->path = path;
    error_out->message = error.message();
    return false;
}

bool write_text_file(
    const std::filesystem::path& path,
    std::string_view contents,
    ProjectExportError* error_out) {
    if (!ensure_output_directory(path, error_out)) {
        return false;
    }

    std::ofstream output(path);
    if (!output) {
        error_out->path = path;
        error_out->message = "failed to open the output file";
        return false;
    }

    output << contents;
    if (!output) {
        error_out->path = path;
        error_out->message = "failed to write the output file";
        return false;
    }

    return true;
}

bool write_binary_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& contents,
    ProjectExportError* error_out) {
    if (!ensure_output_directory(path, error_out)) {
        return false;
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        error_out->path = path;
        error_out->message = "failed to open the output file";
        return false;
    }

    if (!contents.empty()) {
        output.write(
            reinterpret_cast<const char*>(contents.data()),
            static_cast<std::streamsize>(contents.size()));
    }
    if (!output) {
        error_out->path = path;
        error_out->message = "failed to write the output file";
        return false;
    }

    return true;
}

bool copy_file_if_needed(
    const std::filesystem::path& source_path,
    const std::filesystem::path& destination_path,
    bool* copied_out,
    ProjectExportError* error_out) {
    *copied_out = false;
    std::error_code error;
    const bool source_exists = std::filesystem::exists(source_path, error);
    if (error) {
        error_out->path = source_path;
        error_out->message = error.message();
        return false;
    }
    if (!source_exists) {
        return true;
    }

    const bool same_target =
        source_path.lexically_normal() == destination_path.lexically_normal();
    if (same_target) {
        *copied_out = true;
        return true;
    }

    if (!ensure_output_directory(destination_path, error_out)) {
        return false;
    }

    std::filesystem::copy_file(
        source_path,
        destination_path,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (!error) {
        *copied_out = true;
        return true;
    }

    error_out->path = destination_path;
    error_out->message = error.message();
    return false;
}

bool export_atlas_asset(
    const std::filesystem::path& source_atlas_path,
    const std::filesystem::path& output_directory,
    std::filesystem::path* exported_atlas_path_out,
    std::vector<std::filesystem::path>* exported_texture_paths_out,
    ProjectExportError* error_out) {
    const auto atlas_document_result = marrow::runtime::json::load_document(source_atlas_path);
    if (!atlas_document_result) {
        error_out->path = source_atlas_path;
        error_out->message = atlas_document_result.error->format();
        return false;
    }

    const auto atlas_result = AtlasLoader::load(*atlas_document_result.document);
    if (!atlas_result) {
        error_out->path = source_atlas_path;
        error_out->message = atlas_result.error->format();
        return false;
    }

    Document atlas_document = *atlas_document_result.document;
    const std::filesystem::path exported_atlas_path =
        (output_directory / source_atlas_path.filename()).lexically_normal();

    if (Value* atlas_object = marrow::runtime::json::find_member(atlas_document.root, "atlas");
        atlas_object != nullptr && atlas_object->is_object()) {
        if (Value* image_value = marrow::runtime::json::find_member(*atlas_object, "image");
            image_value != nullptr && image_value->is_string() &&
            !image_value->as_string().empty()) {
            const std::filesystem::path declared_image_path = image_value->as_string();
            const std::filesystem::path resolved_image_path =
                declared_image_path.is_absolute()
                    ? declared_image_path.lexically_normal()
                    : (source_atlas_path.parent_path() / declared_image_path).lexically_normal();
            const std::filesystem::path exported_image_path =
                (output_directory / resolved_image_path.filename()).lexically_normal();

            bool copied_texture = false;
            ProjectExportError texture_error = *error_out;
            if (!copy_file_if_needed(
                    resolved_image_path,
                    exported_image_path,
                    &copied_texture,
                    &texture_error)) {
                *error_out = std::move(texture_error);
                return false;
            }
            if (copied_texture) {
                image_value->as_string() = exported_image_path.filename().generic_string();
                exported_texture_paths_out->push_back(exported_image_path);
            }
        }
    }

    if (!write_text_file(
            exported_atlas_path,
            marrow::runtime::json::serialize_pretty(atlas_document.root),
            error_out)) {
        return false;
    }

    *exported_atlas_path_out = exported_atlas_path;
    return true;
}

bool export_packed_atlas_asset(
    const ProjectData& project,
    const AtlasPackDefinition& definition,
    const std::filesystem::path& output_directory,
    std::filesystem::path* exported_atlas_path_out,
    std::vector<std::filesystem::path>* exported_texture_paths_out,
    ProjectExportError* error_out) {
    const std::filesystem::path exported_atlas_path =
        (output_directory / definition.atlas_path.filename()).lexically_normal();
    const auto artifact_result = detail::build_packed_atlas_artifact(
        project,
        definition,
        exported_atlas_path);
    if (!artifact_result) {
        error_out->path = exported_atlas_path;
        error_out->message = artifact_result.error_message;
        return false;
    }

    if (!write_binary_file(
            artifact_result.artifact->image_path,
            artifact_result.artifact->image_bytes,
            error_out)) {
        return false;
    }
    if (!write_text_file(
            artifact_result.artifact->atlas_path,
            artifact_result.artifact->atlas_text,
            error_out)) {
        return false;
    }

    exported_texture_paths_out->push_back(artifact_result.artifact->image_path);
    *exported_atlas_path_out = artifact_result.artifact->atlas_path;
    return true;
}

} // namespace

ProjectLoadResult load_project(const Document& document) {
    ProjectLoadResult result;
    ProjectData project;
    if (const auto error = parse_project_document(document, &project)) {
        result.error = error;
        return result;
    }

    auto project_ptr = std::make_shared<ProjectData>(std::move(project));
    const auto document_result =
        marrow::runtime::load_skeleton_document(project_ptr->resolved_skeleton_path());
    if (!document_result) {
        result.error = document_result.error;
        return result;
    }

    result.base_skeleton_document =
        marrow::allocate_shared<Document>(std::move(*document_result.document));
    const ProjectRuntimeResult runtime_result =
        build_project_runtime(*project_ptr, *result.base_skeleton_document);
    if (!runtime_result) {
        result.error = runtime_result.error;
        return result;
    }

    std::vector<std::shared_ptr<const marrow::runtime::AtlasData>> atlas_data;
    for (const auto& atlas_path : project_ptr->resolved_atlas_paths()) {
        const auto atlas_result = AtlasLoader::load(atlas_path);
        if (!atlas_result) {
            result.error = atlas_result.error;
            return result;
        }
        atlas_data.push_back(atlas_result.atlas_data);
    }

    result.project = std::move(project_ptr);
    result.skeleton_data = runtime_result.skeleton_data;
    result.atlas_data = std::move(atlas_data);
    return result;
}

ProjectLoadResult load_project(const std::filesystem::path& path) {
    const auto document_result = marrow::runtime::json::load_document(path);
    if (!document_result) {
        ProjectLoadResult result;
        result.error = document_result.error;
        return result;
    }

    return load_project(*document_result.document);
}

ProjectSaveResult save_project(const ProjectData& project, const std::filesystem::path& path) {
    ProjectSaveResult result;
    ProjectSaveError save_error;
    save_error.path = path;
    // Rebase FIRST, so validation, serialization and the returned project all
    // describe the same thing: the project as it will exist at `path`. Doing it
    // here rather than in EditorSession::save is what gives every caller -- the
    // session, the Agent save review path, the shell, and every smoke -- a Save
    // As that produces an openable project. A same-path save is a no-op rebase by
    // construction, since each reference relativizes against its own directory.
    const ProjectData rebased = rebase_project_paths(project, path);
    if (!validate_project_for_save(rebased, &save_error)) {
        result.error = std::move(save_error);
        return result;
    }

    const std::filesystem::path parent_path = path.parent_path();
    if (!parent_path.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent_path, error);
        if (error) {
            save_error.message = error.message();
            result.error = std::move(save_error);
            return result;
        }
    }

    // Write through a temporary in the destination's own directory and replace
    // the destination with one atomic rename. A direct `std::ofstream output(path)`
    // TRUNCATES the destination before anything knows the new content is
    // writable, and `if (!output)` runs BEFORE `~ofstream` flushes -- so a
    // close-time write error (the ordinary shape of a full filesystem) was never
    // observed and this function returned SUCCESS over a truncated file, after
    // which EditorSession::save marked the session clean. This primitive checks
    // write, flush AND close, and leaves the destination byte-for-byte unchanged
    // on every handled failure. It does not fsync: durability across power loss
    // is a stated non-goal, and a crash between the temporary's creation and the
    // rename can leave one orphan `*.tmp.*` file beside the project.
    const std::string write_error = detail::write_file_atomically(
        path,
        serialize_project(rebased),
        "project");
    if (!write_error.empty()) {
        save_error.message = write_error;
        result.error = std::move(save_error);
        return result;
    }

    ProjectData saved_project = rebased;
    saved_project.source_path = path;
    result.project = std::make_shared<ProjectData>(std::move(saved_project));
    return result;
}

ProjectExportResult export_runtime_assets(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    const ProjectExportOptions& options) {
    ProjectExportResult result;
    result.path = options.skeleton_output_path.empty() ? project.resolved_export_skeleton_path()
                                                       : options.skeleton_output_path.lexically_normal();

    ProjectExportError export_error;
    export_error.path = result.path;

    if (const auto animation_error =
            validate_animation_edit_sequence(project, base_skeleton_document)) {
        export_error.message = animation_error->format();
        result.error = std::move(export_error);
        return result;
    }

    if (const auto lifecycle_error =
            validate_constraint_lifecycle_operations(project, base_skeleton_document)) {
        export_error.message = lifecycle_error->format();
        result.error = std::move(export_error);
        return result;
    }

    const Document runtime_document = build_project_runtime_document(project, base_skeleton_document);
    const auto skeleton_result = marrow::runtime::load_skeleton_data(runtime_document);
    if (!skeleton_result) {
        export_error.message = skeleton_result.error->format();
        result.error = std::move(export_error);
        return result;
    }

    if (!write_text_file(
            result.path,
            marrow::runtime::json::serialize_pretty(runtime_document.root),
            &export_error)) {
        result.error = std::move(export_error);
        return result;
    }

    const std::filesystem::path output_directory =
        result.path.has_parent_path() ? result.path.parent_path() : std::filesystem::path(".");
    for (const auto& atlas_path : project.resolved_atlas_paths()) {
        std::filesystem::path exported_atlas_path;
        const AtlasPackDefinition* atlas_pack_definition =
            project.find_atlas_pack_definition(atlas_path);
        const bool exported =
            atlas_pack_definition != nullptr
                ? export_packed_atlas_asset(
                      project,
                      *atlas_pack_definition,
                      output_directory,
                      &exported_atlas_path,
                      &result.texture_paths,
                      &export_error)
                : export_atlas_asset(
                      atlas_path,
                      output_directory,
                      &exported_atlas_path,
                      &result.texture_paths,
                      &export_error);
        if (!exported) {
            result.error = std::move(export_error);
            return result;
        }
        result.atlas_paths.push_back(std::move(exported_atlas_path));
    }

    if (options.binary_output_path.has_value()) {
        result.binary_path = options.binary_output_path->empty()
                                 ? default_export_binary_path(result.path)
                                 : options.binary_output_path->lexically_normal();
        if (!ensure_output_directory(*result.binary_path, &export_error)) {
            result.error = std::move(export_error);
            return result;
        }
        if (const auto binary_error = marrow::runtime::write_skeleton_binary_document(
                runtime_document,
                *result.binary_path)) {
            export_error.path = *result.binary_path;
            export_error.message = binary_error->format();
            result.error = std::move(export_error);
            return result;
        }
    }

    return result;
}

ProjectExportResult export_runtime_skeleton(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    const std::filesystem::path& output_path) {
    ProjectExportOptions options;
    options.skeleton_output_path = output_path;
    return export_runtime_assets(project, base_skeleton_document, options);
}

} // namespace marrow::editor
