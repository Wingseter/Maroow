#include "agent_dispatch_internal.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "marrow/editor/psd_reimport_plan.hpp"

namespace marrow::editor::agent_detail {

namespace {

const char* psd_change_name(PsdLayerChangeKind change) {
    switch (change) {
        case PsdLayerChangeKind::Added:
            return "added";
        case PsdLayerChangeKind::Updated:
            return "updated";
        case PsdLayerChangeKind::Missing:
            return "missing";
    }
    return "unknown";
}

/// @brief A unique staging directory under @p root, so a re-run never inherits one.
std::filesystem::path unique_staging_directory(const std::filesystem::path& root) {
    static std::uint64_t sequence = 0U;
    return root / ("plan-" + std::to_string(++sequence));
}

} // namespace

std::string plan_project_reimport(
    EditorSession& session,
    const std::filesystem::path& psd_path,
    const std::filesystem::path& staging_root,
    PsdReimportPlan* plan_out) {
    const ProjectData& project = *session.project();
    const std::vector<std::filesystem::path> atlases = project.resolved_atlas_paths();
    if (atlases.empty()) {
        return "The project references no atlas.";
    }
    // The staged bundle carries the TARGET's own names. The atlas document's
    // `image` member is a bare PNG name resolved beside the atlas file, and the
    // packer writes it from the staged atlas path -- so staging under any other
    // name produces a document that copies byte-perfectly and names a texture that
    // is not there. The atlas file name follows the CURRENT texture's stem, not the
    // atlas's own: `player_idle.matl` declares `"image": "player_fixture.png"`, and
    // staging under the atlas stem would orphan that PNG.
    const auto atlas_document = runtime::json::load_document(atlases.front());
    if (!atlas_document) {
        return "The project atlas did not parse: " + atlas_document.error->message;
    }
    const json::Value* atlas_object =
        runtime::json::find_member(atlas_document.document->root, "atlas");
    const json::Value* image_member = atlas_object != nullptr && atlas_object->is_object()
        ? runtime::json::find_member(*atlas_object, "image")
        : nullptr;
    if (image_member == nullptr || !image_member->is_string() ||
        image_member->as_string().empty()) {
        return "The project atlas declares no 'image' member.";
    }
    const std::filesystem::path image(image_member->as_string());

    PsdReimportPlanOptions options;
    options.psd_path = psd_path;
    options.staging_root = unique_staging_directory(staging_root);
    options.staged_skeleton_filename =
        project.resolved_skeleton_path().filename().generic_string();
    options.staged_atlas_filename = image.stem().generic_string() + ".matl";
    *plan_out = plan_psd_reimport(project, options);
    if (!*plan_out) {
        std::error_code error;
        std::filesystem::remove_all(options.staging_root, error);
        return plan_out->error->format();
    }
    return {};
}

std::string psd_plan_digest(const PsdReimportPlan& plan) {
    // The ordered row list, which is what the reviewer saw. A count would not
    // change when two layers swap classification.
    std::string digest;
    for (const PsdPlannedLayer& layer : plan.layers) {
        digest += layer.identity;
        digest += '=';
        digest += psd_change_name(layer.change);
        digest += ';';
    }
    return digest;
}

json::Value psd_plan_value(const PsdReimportPlan& plan, const std::string& digest) {
    json::Value::Array rows;
    rows.reserve(plan.layers.size());
    for (const PsdPlannedLayer& layer : plan.layers) {
        json::Value::Object row;
        row.emplace("identity", string_value(layer.identity));
        row.emplace("change", string_value(psd_change_name(layer.change)));
        row.emplace("current_slot", string_value(layer.current_slot_name));
        row.emplace("current_attachment", string_value(layer.current_attachment_name));
        row.emplace("current_bone", string_value(layer.current_bone_name));
        row.emplace("current_image", string_value(layer.current_image_file));
        row.emplace("proposed_slot", string_value(layer.proposed_slot_name));
        row.emplace("proposed_attachment", string_value(layer.proposed_attachment_name));
        row.emplace("proposed_bone", string_value(layer.proposed_bone_name));
        row.emplace("proposed_image", string_value(layer.proposed_image_file));
        row.emplace("preserve", bool_value(layer.preserve));
        rows.push_back(object_value(std::move(row)));
    }
    json::Value::Object value;
    value.emplace("layers", array_value(std::move(rows)));
    value.emplace("added", number_value(plan.added_count));
    value.emplace("updated", number_value(plan.updated_count));
    value.emplace("missing", number_value(plan.missing_count));
    value.emplace("digest", string_value(digest));
    value.emplace("staged_skeleton", string_value(plan.staged_skeleton_path.string()));
    value.emplace("staged_atlas", string_value(plan.staged_atlas_path.string()));
    value.emplace("staged_texture", string_value(plan.staged_texture_path.string()));
    value.emplace("staged_layers", string_value(plan.staged_layers_directory.string()));
    return object_value(std::move(value));
}

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
        // Terminate is the user's kill switch: a terminated session must not
        // be able to restore its own access. Only editor-side user action
        // clears the terminated flag.
        if (control.terminated) {
            return make_error(
                "Agent session was terminated; only the editor can restore access.",
                op,
                spec,
                "terminated");
        }
        control.paused = false;
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
            // MAR-189: the targets come from the PROJECT. This op is now the
            // reimport-commit path, and a reimport into /tmp is not a reimport of
            // anything. Supplying `output` or `atlas_output` is still allowed --
            // they must simply name the project's own bundle, which is what makes
            // a wrong path an explicit refusal rather than a silent redirect.
            const std::vector<std::filesystem::path> project_atlases =
                project.resolved_atlas_paths();
            if (project_atlases.empty()) {
                return make_error(
                    "import.psd_layers requires a project that references an atlas.",
                    op,
                    spec,
                    "not_project_bundle");
            }
            const std::filesystem::path project_skeleton =
                absolute_normalized(project.resolved_skeleton_path());
            const std::filesystem::path project_atlas =
                absolute_normalized(project_atlases.front());
            const auto requested_skeleton =
                path_arg_any(*args, {"output", "skeleton_output", "target"});
            const auto requested_atlas = path_arg_any(*args, {"atlas_output"});
            if (requested_skeleton.has_value() &&
                absolute_normalized(*requested_skeleton) != project_skeleton) {
                return make_error(
                    "import.psd_layers replaces the project's own bundle; '" +
                        absolute_normalized(*requested_skeleton).string() +
                        "' is not '" + project_skeleton.string() + "'.",
                    op,
                    spec,
                    "not_project_bundle");
            }
            if (requested_atlas.has_value() &&
                absolute_normalized(*requested_atlas) != project_atlas) {
                return make_error(
                    "import.psd_layers replaces the project's own bundle; '" +
                        absolute_normalized(*requested_atlas).string() + "' is not '" +
                        project_atlas.string() + "'.",
                    op,
                    spec,
                    "not_project_bundle");
            }
            targets.push_back(project_skeleton);
            targets.push_back(project_atlas);
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

        // MAR-189: `import.psd_layers` returns the PLAN, dry run or not. The
        // staging root is a write target and is whitelist-checked like any other.
        std::string plan_digest;
        if (op == "import.psd_layers") {
            const std::filesystem::path staging_root =
                path_arg_any(*args, {"staging_root"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_psd_reimport"));
            if (!agent_path_allowed(session, staging_root)) {
                return make_error(
                    "Staging root is outside the agent whitelist.",
                    op,
                    spec,
                    "forbidden_path");
            }
            PsdReimportPlan plan;
            const std::string staging_error =
                plan_project_reimport(session, *input_path, staging_root, &plan);
            if (!staging_error.empty()) {
                return make_error(staging_error, op, spec, "psd_plan_failed");
            }
            plan_digest = psd_plan_digest(plan);
            preview.emplace("plan", psd_plan_value(plan, plan_digest));
            // The staging root goes away in BOTH directions. A dry run must leave
            // nothing behind, and a queued review must not own a filesystem
            // lifetime across an unbounded human wait -- approval re-plans into a
            // fresh root and compares the digest instead.
            std::error_code staging_cleanup;
            std::filesystem::remove_all(plan.staging_root, staging_cleanup);
        }

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
            std::move(summary),
            input_path.value_or(std::filesystem::path()),
            std::move(plan_digest));
    }

    return make_error(
        "Unknown operation: " + std::string(op),
        op,
        spec,
        "unknown_operation");
}

} // namespace marrow::editor::agent_detail
