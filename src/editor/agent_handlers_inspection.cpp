#include "agent_dispatch_internal.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <variant>

#include "marrow/editor/diagnostics.hpp"
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
        // MAR-175: the per-vertex influence values, so a headless smoke can
        // assert what a weight mutation actually wrote and -- more importantly
        // -- that the vertices it did NOT name are untouched. Additive: every
        // shipped field keeps its name, type, and position.
        json::Value::Array weight_rows;
        weight_rows.reserve(attachment->mesh_geometry->weights.size());
        for (const auto& runtime_vertex : attachment->mesh_geometry->weights) {
            json::Value::Array influences;
            influences.reserve(runtime_vertex.influences.size());
            for (const auto& influence : runtime_vertex.influences) {
                json::Value::Object entry;
                entry.emplace(
                    "bone",
                    string_value(
                        influence.bone_index < skeleton.bones().size()
                            ? skeleton.bones()[influence.bone_index].name
                            : std::string{}));
                entry.emplace("x", number_value(influence.x));
                entry.emplace("y", number_value(influence.y));
                entry.emplace("weight", number_value(influence.weight));
                influences.push_back(object_value(std::move(entry)));
            }
            weight_rows.push_back(array_value(std::move(influences)));
        }
        mesh.emplace("weights", array_value(std::move(weight_rows)));
        return make_success("Mesh described", op, spec, object_value(std::move(mesh)));
    }

    if (op == "project.diagnostics") {
        const auto report = collect_session_diagnostics(session);
        if (!report.has_value()) {
            // Unreachable through the dispatcher: `ensure_project_loaded`
            // rejects first. Written to mirror `runtime.validate`'s own
            // null-document guard rather than left to crash.
            return make_error(
                "Project diagnostics are unavailable.",
                op,
                spec,
                "diagnostics_unavailable");
        }

        json::Value::Object diagnostics;
        // The four legacy members keep their exact names and JSON types. Two of
        // them keep their exact expressions as well -- `project_dirty` and
        // `review_queue_count`, below -- but `error_count` and `warning_count`
        // do NOT: they were a hardcoded `0` and `session.dirty() ? 1 : 0`, and
        // they are now severity counts over the report. What is preserved is
        // their VALUE, not their expression, and only on a project with no
        // other issues: a clean project is 0/0 and a dirty one is 0/1, because
        // `collect_session_diagnostics` emits a `project.unsaved_changes`
        // Warning exactly when `session.dirty()`. Every project any shipped
        // suite runs against is one of those two.
        diagnostics.emplace("error_count", number_value(report->error_count));
        diagnostics.emplace("warning_count", number_value(report->warning_count));
        diagnostics.emplace("project_dirty", bool_value(session.dirty()));
        // Verbatim from the shipped branch, not re-derived. The review queue is
        // a permissions concept, not a project defect, so it is deliberately
        // NOT an issue -- turning it into one would move `warning_count`
        // whenever an agent queues a save, and a shipped case asserts this
        // payload is unchanged across queueing six reviews.
        diagnostics.emplace("review_queue_count", number_value(control.review_queue.size()));

        json::Value::Array issues;
        issues.reserve(report->issues.size());
        for (const DiagnosticIssue& issue : report->issues) {
            json::Value::Object entry;
            entry.emplace("code", string_value(std::string(diagnostic_code_name(issue.code))));
            if (issue.family != DiagnosticOverlayFamily::None) {
                entry.emplace(
                    "family",
                    string_value(std::string(diagnostic_overlay_family_name(issue.family))));
            }
            entry.emplace(
                "severity",
                string_value(std::string(diagnostic_severity_name(issue.severity))));
            entry.emplace("identity", string_value(issue.identity));
            entry.emplace("message", string_value(issue.message));

            json::Value::Object target;
            target.emplace(
                "panel",
                string_value(std::string(diagnostic_panel_name(issue.target.panel))));
            if (!issue.target.animation_name.empty()) {
                target.emplace("animation", string_value(issue.target.animation_name));
            }
            if (issue.target.vertex_index.has_value()) {
                target.emplace("vertex_index", number_value(*issue.target.vertex_index));
            }
            if (issue.target.selection.has_value()) {
                json::Value::Object selection;
                if (const auto* bone = std::get_if<BoneSelection>(&*issue.target.selection)) {
                    selection.emplace("kind", string_value("bone"));
                    selection.emplace("bone", string_value(bone->bone_name));
                } else if (const auto* slot =
                               std::get_if<SlotSelection>(&*issue.target.selection)) {
                    selection.emplace("kind", string_value("slot"));
                    selection.emplace("slot", string_value(slot->slot_name));
                } else if (const auto* attachment =
                               std::get_if<AttachmentSelection>(&*issue.target.selection)) {
                    selection.emplace("kind", string_value("attachment"));
                    selection.emplace("slot", string_value(attachment->slot_name));
                    selection.emplace("skin", string_value(attachment->skin_name));
                    selection.emplace(
                        "attachment", string_value(attachment->attachment_name));
                }
                target.emplace("selection", object_value(std::move(selection)));
            }
            entry.emplace("target", object_value(std::move(target)));

            // An absent safe fix OMITS the member rather than emitting "". An
            // empty string is a value a careless consumer treats as present.
            if (!issue.safe_fix_id.empty()) {
                entry.emplace("safe_fix_id", string_value(issue.safe_fix_id));
            }
            issues.push_back(object_value(std::move(entry)));
        }
        // From the REPORT, not from the array just built. Deriving it from the
        // array would make the wire assertion `issue_count == len(issues)`
        // incapable of failing -- a truncating serializer would keep them
        // consistent with each other while dropping issues.
        diagnostics.emplace("issue_count", number_value(report->issues.size()));
        diagnostics.emplace("issues", array_value(std::move(issues)));

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
