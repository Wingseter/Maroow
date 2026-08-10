#include "agent_dispatch_internal.hpp"
#include "shell_constraints.hpp"

#include <string>
#include <utility>

namespace marrow::editor::agent_detail {

AgentDispatchResult handle_constraint_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation) {
    EditorSession& session = context.session;
    const std::string_view op = operation.name;
    const OperationSpec* spec = &operation;
    const ProjectData& project = *session.project();
    const marrow::runtime::SkeletonData& skeleton = *session.runtime_data();

    if (op == "edit_ik_constraint") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("edit_ik_constraint requires 'args' object.", op, spec);
        }

        const auto name = string_arg(*args, "name");
        if (!name.has_value()) {
            return make_error("edit_ik_constraint requires 'name' string.", op, spec);
        }

        if (bool_arg(args, "dry_run")) {
            const IkConstraintEdit* project_edit = project.find_ik_constraint_edit(*name);
            const marrow::runtime::IkConstraintData* runtime_constraint =
                shell::find_named_constraint(skeleton.ik_constraints(), *name);
            if (project_edit == nullptr && runtime_constraint == nullptr) {
                return make_error(
                    "IK constraint not found in runtime skeleton.",
                    op,
                    spec,
                    "not_found");
            }
            json::Value::Object preview;
            preview.emplace("dry_run", bool_value(true));
            preview.emplace("name", string_value(std::string(*name)));
            if (const auto mix = number_arg(*args, "mix")) {
                preview.emplace("mix", number_value(*mix));
            } else if (project_edit != nullptr) {
                preview.emplace("mix", number_value(project_edit->mix));
            } else {
                preview.emplace("mix", number_value(runtime_constraint->mix));
            }
            return make_success(
                "IK constraint edit validated.",
                op,
                spec,
                object_value(std::move(preview)));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Edit IK Constraint via Agent",
            "Agent",
            bool_arg(args, "merge"),
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& editable_project = *transaction.project();
        IkConstraintEdit* edit = editable_project.find_ik_constraint_edit(*name);
        if (edit == nullptr) {
            const marrow::runtime::IkConstraintData* runtime_constraint =
                shell::find_named_constraint(skeleton.ik_constraints(), *name);
            if (runtime_constraint == nullptr) {
                return make_error(
                    "IK constraint not found in runtime skeleton.",
                    op,
                    spec,
                    "not_found");
            }

            IkConstraintEdit new_edit;
            new_edit.name = std::string(*name);
            new_edit.bone_names =
                names_from_indices(skeleton.bones(), runtime_constraint->bone_indices);
            if (runtime_constraint->target_bone_index < skeleton.bones().size()) {
                new_edit.target_bone_name =
                    skeleton.bones()[runtime_constraint->target_bone_index].name;
            }
            new_edit.mix = runtime_constraint->mix;
            new_edit.bend_positive = runtime_constraint->bend_positive;
            new_edit.softness = runtime_constraint->softness;
            new_edit.compress = runtime_constraint->compress;
            new_edit.stretch = runtime_constraint->stretch;

            editable_project.ik_constraint_edits.push_back(std::move(new_edit));
            edit = &editable_project.ik_constraint_edits.back();
        }

        if (const json::Value* target_val = json::find_member(*args, "target")) {
            if (target_val->is_string()) {
                edit->target_bone_name = target_val->as_string();
            } else if (!target_val->is_null()) {
                return make_error("target must be string or null.", op, spec);
            }
        }

        if (const json::Value* bones_val = json::find_member(*args, "bone_names")) {
            if (!bones_val->is_array()) {
                return make_error("bone_names must be an array of strings.", op, spec);
            }
            edit->bone_names.clear();
            for (const auto& bone : bones_val->as_array()) {
                if (!bone.is_string()) {
                    return make_error("bone_names must be an array of strings.", op, spec);
                }
                edit->bone_names.push_back(bone.as_string());
            }
        }

        if (const json::Value* mix_val = json::find_member(*args, "mix")) {
            if (mix_val->is_number()) {
                edit->mix = mix_val->as_number();
            } else if (!mix_val->is_null()) {
                return make_error("mix must be number or null.", op, spec);
            }
        }

        if (const json::Value* bend_val = json::find_member(*args, "bend_positive")) {
            if (bend_val->is_boolean()) {
                edit->bend_positive = bend_val->as_boolean();
            } else if (!bend_val->is_null()) {
                return make_error("bend_positive must be bool or null.", op, spec);
            }
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply IK constraint edit: " + commit_result.error->format(),
                op,
                spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        return make_success("Edited IK constraint successfully.", op, spec);
    }

    if (op == "edit_path_constraint") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("edit_path_constraint requires 'args' object.", op, spec);
        }
        const auto name = string_arg(*args, "name");
        if (!name.has_value()) {
            return make_error("edit_path_constraint requires 'name' string.", op, spec);
        }

        PathConstraintEdit merged;
        if (const PathConstraintEdit* existing = project.find_path_constraint_edit(*name)) {
            merged = *existing;
        } else if (const auto* runtime_constraint =
                       find_runtime_path_constraint(skeleton, *name)) {
            merged = path_constraint_edit_from_runtime(skeleton, *runtime_constraint);
        } else {
            return make_error("Path constraint not found.", op, spec, "not_found");
        }

        std::string merge_error;
        if (!merge_path_constraint_args(*args, skeleton, &merged, &merge_error)) {
            return make_error(std::move(merge_error), op, spec);
        }
        if (bool_arg(args, "dry_run")) {
            return make_success(
                "Path constraint edit validated.",
                op,
                spec,
                path_constraint_preview_value(merged, true));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Edit path constraint via Agent",
            "Agent",
            bool_arg(args, "merge"),
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& editable_project = *transaction.project();
        if (PathConstraintEdit* edit = editable_project.find_path_constraint_edit(*name)) {
            *edit = std::move(merged);
        } else {
            editable_project.path_constraint_edits.push_back(std::move(merged));
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply path constraint edit: " + commit_result.error->format(),
                op,
                spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success(
            "Edited path constraint successfully.",
            op,
            spec,
            path_constraint_preview_value(
                *session.project()->find_path_constraint_edit(*name),
                false));
    }

    if (op == "edit_transform_constraint") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("edit_transform_constraint requires 'args' object.", op, spec);
        }
        const auto name = string_arg(*args, "name");
        if (!name.has_value()) {
            return make_error("edit_transform_constraint requires 'name' string.", op, spec);
        }

        TransformConstraintEdit merged;
        if (const TransformConstraintEdit* existing =
                project.find_transform_constraint_edit(*name)) {
            merged = *existing;
        } else if (const auto* runtime_constraint =
                       find_runtime_transform_constraint(skeleton, *name)) {
            merged = transform_constraint_edit_from_runtime(skeleton, *runtime_constraint);
        } else {
            return make_error("Transform constraint not found.", op, spec, "not_found");
        }

        std::string merge_error;
        if (!merge_transform_constraint_args(*args, skeleton, &merged, &merge_error)) {
            return make_error(std::move(merge_error), op, spec);
        }
        if (bool_arg(args, "dry_run")) {
            return make_success(
                "Transform constraint edit validated.",
                op,
                spec,
                transform_constraint_preview_value(merged, true));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Edit transform constraint via Agent",
            "Agent",
            bool_arg(args, "merge"),
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& editable_project = *transaction.project();
        if (TransformConstraintEdit* edit =
                editable_project.find_transform_constraint_edit(*name)) {
            *edit = std::move(merged);
        } else {
            editable_project.transform_constraint_edits.push_back(std::move(merged));
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply transform constraint edit: " + commit_result.error->format(),
                op,
                spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success(
            "Edited transform constraint successfully.",
            op,
            spec,
            transform_constraint_preview_value(
                *session.project()->find_transform_constraint_edit(*name),
                false));
    }

    if (op == "edit_physics_constraint") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("edit_physics_constraint requires 'args' object.", op, spec);
        }
        const auto name = string_arg(*args, "name");
        if (!name.has_value()) {
            return make_error("edit_physics_constraint requires 'name' string.", op, spec);
        }

        PhysicsConstraintEdit merged;
        if (const PhysicsConstraintEdit* existing = project.find_physics_constraint_edit(*name)) {
            merged = *existing;
        } else if (const auto* runtime_constraint =
                       find_runtime_physics_constraint(skeleton, *name)) {
            merged = physics_constraint_edit_from_runtime(skeleton, *runtime_constraint);
        } else {
            return make_error("Physics constraint not found.", op, spec, "not_found");
        }

        std::string merge_error;
        if (!merge_physics_constraint_args(*args, skeleton, &merged, &merge_error)) {
            return make_error(std::move(merge_error), op, spec);
        }
        if (bool_arg(args, "dry_run")) {
            return make_success(
                "Physics constraint edit validated.",
                op,
                spec,
                physics_constraint_preview_value(merged, true));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Edit physics constraint via Agent",
            "Agent",
            bool_arg(args, "merge"),
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& editable_project = *transaction.project();
        if (PhysicsConstraintEdit* edit =
                editable_project.find_physics_constraint_edit(*name)) {
            *edit = std::move(merged);
        } else {
            editable_project.physics_constraint_edits.push_back(std::move(merged));
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply physics constraint edit: " + commit_result.error->format(),
                op,
                spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success(
            "Edited physics constraint successfully.",
            op,
            spec,
            physics_constraint_preview_value(
                *session.project()->find_physics_constraint_edit(*name),
                false));
    }

    return make_error(
        "Unknown operation: " + std::string(op),
        op,
        spec,
        "unknown_operation");
}

} // namespace marrow::editor::agent_detail
