#include "agent_dispatch_internal.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "marrow/runtime/animation_compare.hpp"

namespace marrow::editor::agent_detail {

AgentDispatchResult handle_inspection_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation) {
    EditorSession& session = context.session;
    AgentControlState& control = context.control;
    const std::string_view op = operation.name;
    const OperationSpec* spec = &operation;
    const ProjectData& project = *session.project();
    const marrow::runtime::SkeletonData& skeleton = *session.runtime_data();

    if (op == "export.preview") {
        const json::Value* args = command_args(cmd);
        const bool binary_output = bool_arg(args, "binary");
        json::Value::Object preview;
        preview.emplace("binary", bool_value(binary_output));
        preview.emplace("requires_review", bool_value(true));
        json::Value::Array targets;
        targets.push_back(string_value(project.resolved_export_skeleton_path().string()));
        if (binary_output) {
            targets.push_back(string_value(project.resolved_export_binary_path().string()));
        }
        for (const auto& atlas_path : project.resolved_atlas_paths()) {
            targets.push_back(string_value(atlas_path.string()));
        }
        preview.emplace("targets", array_value(std::move(targets)));
        return make_success(
            "Export preview generated.",
            op,
            spec,
            object_value(std::move(preview)));
    }

    if (op == "runtime.validate") {
        if (session.base_skeleton_document() == nullptr) {
            json::Value::Object payload;
            json::Value::Object diagnostics;
            diagnostics.emplace("error_count", number_value(std::size_t{1}));
            diagnostics.emplace("warning_count", number_value(std::size_t{0}));
            diagnostics.emplace("message", string_value("No base skeleton document is loaded."));
            payload.emplace("diagnostics", object_value(std::move(diagnostics)));
            return make_error_with_delta(
                "Runtime validation failed.",
                op,
                spec,
                object_value(std::move(payload)),
                "validation_failed");
        }
        const auto runtime_result = marrow::editor::build_project_runtime(
            project,
            *session.base_skeleton_document());
        json::Value::Object payload;
        json::Value::Object diagnostics;
        diagnostics.emplace(
            "error_count",
            number_value(static_cast<std::size_t>(runtime_result ? 0U : 1U)));
        diagnostics.emplace("warning_count", number_value(std::size_t{0}));
        if (runtime_result) {
            diagnostics.emplace(
                "bone_count",
                number_value(runtime_result.skeleton_data->bones().size()));
            diagnostics.emplace(
                "slot_count",
                number_value(runtime_result.skeleton_data->slots().size()));
            payload.emplace("diagnostics", object_value(std::move(diagnostics)));
            return make_success(
                "Runtime validation passed.",
                op,
                spec,
                object_value(std::move(payload)));
        }
        diagnostics.emplace(
            "message",
            string_value(runtime_result.error.has_value()
                             ? runtime_result.error->format()
                             : std::string("Unknown runtime validation failure.")));
        payload.emplace("diagnostics", object_value(std::move(diagnostics)));
        return make_error_with_delta(
            "Runtime validation failed.",
            op,
            spec,
            object_value(std::move(payload)),
            "validation_failed");
    }

    if (op == "compare_runtime_export") {
        if (session.base_skeleton_document() == nullptr) {
            return make_error(
                "No base skeleton document is loaded.",
                op,
                spec,
                "validation_failed");
        }
        const json::Value* args = command_args(cmd);
        const bool binary_output = bool_arg(args, "binary", true);
        ProjectExportOptions options;
        options.skeleton_output_path =
            std::filesystem::path("/tmp/marrow_agent_compare_runtime.mskl");
        if (binary_output) {
            options.binary_output_path =
                std::filesystem::path("/tmp/marrow_agent_compare_runtime.mbin");
        }
        const auto export_result = marrow::editor::export_runtime_assets(
            project,
            *session.base_skeleton_document(),
            options);
        if (!export_result) {
            return make_error(
                "Runtime comparison export failed: " +
                    (export_result.error.has_value()
                         ? export_result.error->format()
                         : std::string("unknown export failure")),
                op,
                spec,
                "export_failed");
        }

        const auto json_runtime = marrow::runtime::load_skeleton_data(export_result.path);
        if (!json_runtime) {
            return make_error(json_runtime.error->format(), op, spec, "validation_failed");
        }

        json::Value::Object summary;
        summary.emplace("json_path", string_value(export_result.path.string()));
        summary.emplace("binary", bool_value(binary_output));
        summary.emplace("bone_count", number_value(json_runtime.skeleton_data->bones().size()));
        summary.emplace("slot_count", number_value(json_runtime.skeleton_data->slots().size()));
        std::error_code file_error;
        const auto json_size = std::filesystem::file_size(export_result.path, file_error);
        if (!file_error) {
            summary.emplace("json_bytes", number_value(static_cast<std::size_t>(json_size)));
        }

        if (binary_output) {
            if (!export_result.binary_path.has_value()) {
                return make_error(
                    "Binary comparison export did not produce a binary path.",
                    op,
                    spec);
            }
            const auto binary_runtime =
                marrow::runtime::load_skeleton_data(*export_result.binary_path);
            if (!binary_runtime) {
                return make_error(binary_runtime.error->format(), op, spec, "validation_failed");
            }
            const auto comparison = marrow::runtime::compare_animation_roundtrip(
                *json_runtime.skeleton_data,
                *binary_runtime.skeleton_data);
            if (!comparison) {
                return make_error(
                    "Animation comparison failed: " + *comparison.error,
                    op,
                    spec,
                    "validation_failed");
            }
            summary.emplace(
                "binary_path",
                string_value(export_result.binary_path->string()));
            file_error.clear();
            const auto binary_size =
                std::filesystem::file_size(*export_result.binary_path, file_error);
            if (!file_error) {
                summary.emplace(
                    "binary_bytes",
                    number_value(static_cast<std::size_t>(binary_size)));
            }
            summary.emplace(
                "rotation_error_degrees",
                number_value(comparison.metrics.max_rotation_error_degrees));
            summary.emplace(
                "position_error_pixels",
                number_value(comparison.metrics.max_translation_error_pixels));
            summary.emplace(
                "rotate_keyframes",
                number_value(comparison.metrics.roundtrip_rotation_keyframes));
        } else {
            summary.emplace("rotation_error_degrees", number_value(0.0));
            summary.emplace("position_error_pixels", number_value(0.0));
        }

        return make_success(
            "Runtime export comparison passed.",
            op,
            spec,
            object_value(std::move(summary)));
    }

    if (op == "scene.describe") {
        json::Value::Object scene_desc;
        scene_desc.emplace("path", string_value(project.source_path.string()));
        scene_desc.emplace("name", string_value(project.editor_metadata.name));
        scene_desc.emplace(
            "export_directory",
            string_value(project.editor_metadata.export_directory.string()));
        scene_desc.emplace("bone_count", number_value(skeleton.bones().size()));
        scene_desc.emplace("slot_count", number_value(skeleton.slots().size()));
        scene_desc.emplace("skin_count", number_value(skeleton.skins().size()));
        scene_desc.emplace("animation_count", number_value(skeleton.animations().size()));
        scene_desc.emplace("ik_constraint_count", number_value(skeleton.ik_constraints().size()));
        scene_desc.emplace(
            "path_constraint_count",
            number_value(skeleton.path_constraints().size()));
        scene_desc.emplace(
            "transform_constraint_count",
            number_value(skeleton.transform_constraints().size()));
        scene_desc.emplace(
            "physics_constraint_count",
            number_value(skeleton.physics_constraints().size()));
        scene_desc.emplace("project_dirty", bool_value(session.dirty()));
        return make_success("Scene described", op, spec, object_value(std::move(scene_desc)));
    }

    if (op == "bones.list") {
        json::Value::Array bones_arr;
        for (const auto& bone : skeleton.bones()) {
            bones_arr.push_back(string_value(bone.name));
        }
        return make_success("Bones listed", op, spec, array_value(std::move(bones_arr)));
    }

    if (op == "animation.list") {
        json::Value::Array anim_arr;
        for (const auto& anim : skeleton.animations()) {
            anim_arr.push_back(string_value(anim.name));
        }
        return make_success("Animations listed", op, spec, array_value(std::move(anim_arr)));
    }

    if (op == "slots.list") {
        return make_success("Slots listed", op, spec, slots_value(skeleton));
    }
    if (op == "skins.list") {
        return make_success("Skins listed", op, spec, skins_value(skeleton));
    }
    if (op == "attachments.list") {
        return make_success(
            "Attachments listed",
            op,
            spec,
            attachments_value(skeleton, command_args(cmd)));
    }
    if (op == "constraints.list") {
        return make_success("Constraints listed", op, spec, constraints_value(skeleton));
    }

    if (op == "timeline.describe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("timeline.describe requires args object.", op, spec);
        }
        const auto animation_name = string_arg(*args, "animation");
        if (!animation_name.has_value()) {
            return make_error("timeline.describe requires animation string.", op, spec);
        }
        return make_success(
            "Timeline described",
            op,
            spec,
            timeline_description_value(skeleton, project, *animation_name));
    }

    if (op == "mesh.describe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("mesh.describe requires args object.", op, spec);
        }
        const auto skin_name = string_arg(*args, "skin");
        const auto slot_name = string_arg(*args, "slot");
        const auto attachment_name = string_arg(*args, "attachment");
        if (!skin_name.has_value() || !slot_name.has_value() || !attachment_name.has_value()) {
            return make_error(
                "mesh.describe requires skin, slot, and attachment strings.",
                op,
                spec);
        }
        const auto slot_index = skeleton.find_slot_index(*slot_name);
        const auto* skin = skeleton.find_skin(*skin_name);
        if (!slot_index.has_value() || skin == nullptr) {
            return make_error(
                "mesh.describe target skin or slot not found.",
                op,
                spec,
                "not_found");
        }
        const auto* attachment = skin->find_attachment(*slot_index, *attachment_name);
        if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
            return make_error("mesh.describe target mesh not found.", op, spec, "not_found");
        }
        json::Value::Object mesh;
        mesh.emplace("skin", string_value(std::string(*skin_name)));
        mesh.emplace("slot", string_value(std::string(*slot_name)));
        mesh.emplace("attachment", string_value(std::string(*attachment_name)));
        mesh.emplace("kind", string_value(attachment_kind_name(attachment->kind)));
        mesh.emplace(
            "vertex_count",
            number_value(attachment->mesh_geometry->vertices.size() / 2U));
        mesh.emplace(
            "triangle_count",
            number_value(attachment->mesh_geometry->triangles.size() / 3U));
        mesh.emplace(
            "weighted_vertex_count",
            number_value(attachment->mesh_geometry->weights.size()));
        return make_success("Mesh described", op, spec, object_value(std::move(mesh)));
    }

    if (op == "project.diagnostics") {
        json::Value::Object diagnostics;
        diagnostics.emplace("error_count", number_value(std::size_t{0}));
        diagnostics.emplace(
            "warning_count",
            number_value(static_cast<std::size_t>(session.dirty() ? 1U : 0U)));
        diagnostics.emplace("project_dirty", bool_value(session.dirty()));
        diagnostics.emplace("review_queue_count", number_value(control.review_queue.size()));
        return make_success(
            "Project diagnostics reported",
            op,
            spec,
            object_value(std::move(diagnostics)));
    }

    return make_error(
        "Unknown operation: " + std::string(op),
        op,
        spec,
        "unknown_operation");
}

} // namespace marrow::editor::agent_detail
