#include "agent_dispatch_internal.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace marrow::editor::agent_detail {

AgentDispatchResult handle_management_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation) {
    EditorSession& session = context.session;
    AgentControlState& control = context.control;
    const std::string_view op = operation.name;
    const OperationSpec* spec = &operation;

    if (op == "operations.list") {
        return make_success("Operations listed", op, spec, operation_specs_value());
    }

    if (op == "agent.permissions.describe") {
        json::Value::Object permissions;
        permissions.emplace("paused", bool_value(control.paused));
        permissions.emplace("terminated", bool_value(control.terminated));
        permissions.emplace("local_only", bool_value(true));
        permissions.emplace("review_required_for_file_writes", bool_value(true));
        permissions.emplace("current_op", string_value(control.current_operation));
        permissions.emplace("last_result", string_value(control.last_result));
        permissions.emplace("pending_reviews", number_value(control.review_queue.size()));
        return make_success(
            "Agent permissions described.",
            op,
            spec,
            object_value(std::move(permissions)));
    }

    if (op == "agent.pause") {
        control.paused = true;
        return make_success("Agent paused.", op, spec);
    }

    if (op == "agent.resume") {
        control.paused = false;
        control.terminated = false;
        return make_success("Agent resumed.", op, spec);
    }

    if (op == "agent.terminate") {
        control.terminated = true;
        control.paused = true;
        control.current_operation.clear();
        return make_success("Agent session terminated.", op, spec);
    }

    const ProjectData& project = *session.project();

    if (op == "save") {
        return enqueue_review(
            context,
            op,
            spec,
            AgentReviewKind::SaveProject,
            "Save project",
            project.source_path,
            false);
    }

    if (op == "export_runtime") {
        const json::Value* args = command_args(cmd);
        const bool binary_output = bool_arg(args, "binary");
        return enqueue_review(
            context,
            op,
            spec,
            AgentReviewKind::ExportRuntime,
            "Export runtime assets",
            project.resolved_export_skeleton_path(),
            binary_output);
    }

    if (op == "import.spine_json" ||
        op == "import.spine_atlas" ||
        op == "import.psd_layers" ||
        op == "atlas.pack") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(std::string(op) + " requires 'args' object.", op, spec);
        }

        const bool dry_run = bool_arg(args, "dry_run", true);
        std::optional<std::filesystem::path> input_path;
        std::vector<std::filesystem::path> targets;
        std::string label;
        if (op == "import.spine_json") {
            input_path = path_arg_any(*args, {"input", "json_path", "path"});
            targets.push_back(
                path_arg_any(*args, {"output", "skeleton_output", "target"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_spine_import.mskl")));
            label = "Import Spine JSON";
        } else if (op == "import.spine_atlas") {
            input_path = path_arg_any(*args, {"input", "atlas_path", "path"});
            targets.push_back(
                path_arg_any(*args, {"output", "atlas_output", "target"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_spine_import.matl")));
            label = "Import Spine atlas";
        } else if (op == "import.psd_layers") {
            input_path = path_arg_any(*args, {"input", "psd_path", "path"});
            targets.push_back(
                path_arg_any(*args, {"output", "skeleton_output", "target"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_psd_layers.mskl")));
            targets.push_back(
                path_arg_any(*args, {"atlas_output"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_psd_layers.matl")));
            label = "Import PSD layers";
        } else {
            targets.push_back(
                path_arg_any(*args, {"output", "atlas_output", "atlas_path", "target"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_atlas_pack.matl")));
            label = "Pack atlas";
        }

        if (op != "atlas.pack" && !input_path.has_value()) {
            return make_error(std::string(op) + " requires an input path.", op, spec);
        }
        if (input_path.has_value() && !agent_path_allowed(session, *input_path)) {
            return make_error(
                "Input path is outside the agent whitelist.",
                op,
                spec,
                "forbidden_path");
        }
        for (const auto& target : targets) {
            if (!agent_path_allowed(session, target)) {
                return make_error(
                    "Output path is outside the agent whitelist.",
                    op,
                    spec,
                    "forbidden_path");
            }
        }

        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(dry_run));
        preview.emplace("requires_review", bool_value(true));
        if (input_path.has_value()) {
            preview.emplace("input", string_value(absolute_normalized(*input_path).string()));
        }
        json::Value::Array target_values;
        target_values.reserve(targets.size());
        for (const auto& target : targets) {
            target_values.push_back(string_value(absolute_normalized(target).string()));
        }
        preview.emplace("targets", array_value(std::move(target_values)));
        if (dry_run) {
            return make_success(
                label + " dry-run validated.",
                op,
                spec,
                object_value(std::move(preview)));
        }

        std::string summary = "op=" + std::string(op);
        if (input_path.has_value()) {
            summary += " input=" + absolute_normalized(*input_path).string();
        }
        if (!targets.empty()) {
            summary += " targets=";
            for (std::size_t index = 0; index < targets.size(); ++index) {
                if (index > 0U) {
                    summary += ",";
                }
                summary += absolute_normalized(targets[index]).string();
            }
        }
        return enqueue_review(
            context,
            op,
            spec,
            AgentReviewKind::ImportOrPack,
            std::move(label),
            targets.empty() ? std::filesystem::path() : targets.front(),
            false,
            std::move(targets),
            std::move(summary));
    }

    return make_error(
        "Unknown operation: " + std::string(op),
        op,
        spec,
        "unknown_operation");
}

} // namespace marrow::editor::agent_detail
