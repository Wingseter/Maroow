#include "agent_dispatch_internal.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

#include "marrow/editor/authoring.hpp"
#include "marrow/runtime/parameter_state.hpp"

namespace marrow::editor::agent_detail {

namespace {

bool args_contain_only(
    const json::Value& args,
    std::initializer_list<std::string_view> allowed,
    std::string* unexpected_out) {
    for (const auto& [name, value] : args.as_object()) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), name) == allowed.end()) {
            if (unexpected_out != nullptr) {
                *unexpected_out = name;
            }
            return false;
        }
    }
    return true;
}

bool optional_bool_arg(
    const json::Value& args,
    std::string_view name,
    bool default_value,
    bool* value_out,
    std::string* error_out) {
    const json::Value* value = json::find_member(args, name);
    if (value == nullptr) {
        *value_out = default_value;
        return true;
    }
    if (!value->is_boolean()) {
        *error_out = std::string(name) + " must be a boolean.";
        return false;
    }
    *value_out = value->as_boolean();
    return true;
}

std::string candidate_runtime_error(
    const ProjectData& project,
    const EditorSession& session) {
    if (session.base_skeleton_document() == nullptr) {
        return "No base skeleton document is loaded.";
    }
    const ProjectRuntimeResult result = build_project_runtime(
        project,
        *session.base_skeleton_document());
    if (!result) {
        return result.error.has_value()
            ? result.error->format()
            : std::string("Unknown candidate runtime build failure.");
    }

    runtime::Skeleton preview(result.skeleton_data);
    for (const auto& [parameter_id, value] :
         session.preview_state().direct_parameter_values) {
        if (!preview.set_parameter_value(parameter_id, value)) {
            return "Candidate preview rejected direct parameter '" + parameter_id + "'.";
        }
    }
    runtime::ParameterState composition(result.skeleton_data);
    if (!composition.set_amplitude(session.preview_state().synthetic_amplitude)) {
        return "Candidate preview rejected the synthetic amplitude input.";
    }
    composition.set_phoneme(session.preview_state().synthetic_phoneme);
    if (session.preview_state().active_expression.has_value() &&
        !composition.activate_expression(*session.preview_state().active_expression)) {
        return "Candidate preview rejected active expression '" +
            *session.preview_state().active_expression + "'.";
    }
    if (!composition.update(0.0) || !composition.apply(preview)) {
        return "Candidate preview parameter composition failed.";
    }
    return {};
}

json::Value parameter_result_value(
    std::string_view id,
    double requested,
    double applied,
    bool clamped,
    bool dry_run) {
    json::Value::Object payload;
    payload.emplace("id", string_value(std::string(id)));
    payload.emplace("requested", number_value(requested));
    payload.emplace("applied", number_value(applied));
    payload.emplace("clamped", bool_value(clamped));
    payload.emplace("dry_run", bool_value(dry_run));
    return object_value(std::move(payload));
}

AgentDispatchResult authoring_error(
    const AuthoringResult& result,
    std::string_view op,
    const OperationSpec* spec) {
    json::Value::Object payload;
    if (!result.dependencies.empty()) {
        payload.emplace("dependencies", string_array_value(result.dependencies));
    }
    return result.dependencies.empty()
        ? make_error(result.error, op, spec)
        : make_error_with_delta(
              result.error,
              op,
              spec,
              object_value(std::move(payload)));
}

AgentDispatchResult validate_candidate(
    ProjectData candidate,
    const EditorSession& session,
    const AuthoringResult& result,
    std::string_view op,
    const OperationSpec* spec,
    std::string success_message,
    json::Value::Object payload) {
    if (!result) {
        return authoring_error(result, op, spec);
    }
    const std::string runtime_error = candidate_runtime_error(candidate, session);
    if (!runtime_error.empty()) {
        return make_error(
            "Candidate runtime validation failed: " + runtime_error,
            op,
            spec,
            "validation_failed");
    }
    payload.emplace("dry_run", bool_value(true));
    return make_success(
        std::move(success_message),
        op,
        spec,
        object_value(std::move(payload)));
}

AgentDispatchResult commit_authoring(
    std::string_view op,
    const OperationSpec* spec,
    std::string label,
    const AuthoringResult& result,
    EditorSession::EditTransaction* transaction,
    std::string success_message,
    json::Value::Object payload) {
    if (!result) {
        transaction->cancel();
        return authoring_error(result, op, spec);
    }
    if (!result.changed) {
        transaction->cancel();
        return make_error("No changes made.", op, spec, "no_change");
    }
    const SessionResult commit_result = transaction->commit();
    if (!commit_result) {
        return make_error(
            "Failed to " + std::move(label) + ": " + commit_result.error->format(),
            op,
            spec,
            "validation_failed");
    }
    if (!commit_result.changed) {
        return make_error("No changes made.", op, spec, "no_change");
    }
    payload.emplace("dry_run", bool_value(false));
    return make_success(
        std::move(success_message),
        op,
        spec,
        object_value(std::move(payload)));
}

json::Value parameter_definition_value(
    const runtime::ParameterDefinition& definition,
    std::size_t index,
    double direct_value,
    double final_value) {
    json::Value::Object payload;
    payload.emplace("index", number_value(index));
    payload.emplace("id", string_value(definition.id));
    payload.emplace("name", string_value(definition.name));
    payload.emplace("min", number_value(definition.min_value));
    payload.emplace("max", number_value(definition.max_value));
    payload.emplace("default", number_value(definition.default_value));
    payload.emplace(
        "type",
        string_value(
            definition.type == runtime::ParameterType::Discrete
                ? "discrete"
                : "continuous"));
    payload.emplace("clamp", bool_value(definition.clamp));
    if (definition.ui_step.has_value()) {
        payload.emplace("ui_step", number_value(*definition.ui_step));
    }
    if (definition.units.has_value()) {
        payload.emplace("units", string_value(*definition.units));
    }
    payload.emplace("direct", number_value(direct_value));
    payload.emplace("final", number_value(final_value));
    return object_value(std::move(payload));
}

json::Value parameter_group_value(
    const runtime::ParameterGroupDefinition& definition,
    std::size_t index) {
    json::Value::Object payload;
    payload.emplace("index", number_value(index));
    payload.emplace("id", string_value(definition.id));
    payload.emplace("name", string_value(definition.name));
    payload.emplace("parameters", string_array_value(definition.parameter_ids));
    payload.emplace("collapsed", bool_value(definition.collapsed));
    if (definition.color_tag.has_value()) {
        payload.emplace("color_tag", string_value(*definition.color_tag));
    }
    if (definition.exclusive_mode.has_value()) {
        payload.emplace("exclusive_mode", string_value(*definition.exclusive_mode));
    }
    return object_value(std::move(payload));
}

} // namespace

AgentDispatchResult handle_parameter_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation) {
    EditorSession& session = context.session;
    const std::string_view op = operation.name;
    const OperationSpec* spec = &operation;

    if (op == "parameters.list") {
        const json::Value* raw_args = json::find_member(cmd, "args");
        if (raw_args != nullptr && !raw_args->is_object()) {
            return make_error("parameters.list args must be an object.", op, spec);
        }
        if (raw_args != nullptr && !raw_args->as_object().empty()) {
            return make_error("parameters.list does not accept arguments.", op, spec);
        }

        if (session.preview_skeleton() == nullptr) {
            return make_error(
                "Parameter preview is not available.", op, spec, "preview_unavailable");
        }
        const runtime::SkeletonData& data = *session.runtime_data();
        const runtime::Skeleton& skeleton = *session.preview_skeleton();
        const auto& definitions = data.parameters();
        const auto& direct_values = skeleton.direct_parameter_values();
        const auto& final_values = skeleton.parameter_values();
        if (direct_values.size() != definitions.size() ||
            final_values.size() != definitions.size()) {
            return make_error(
                "Preview parameter buffers do not match runtime definitions.",
                op,
                spec,
                "preview_unavailable");
        }

        json::Value::Array definition_values;
        json::Value::Object direct_by_id;
        json::Value::Object final_by_id;
        definition_values.reserve(definitions.size());
        for (std::size_t index = 0; index < definitions.size(); ++index) {
            definition_values.push_back(parameter_definition_value(
                definitions[index], index, direct_values[index], final_values[index]));
            direct_by_id.emplace(definitions[index].id, number_value(direct_values[index]));
            final_by_id.emplace(definitions[index].id, number_value(final_values[index]));
        }

        const auto& groups = data.parameter_groups();
        json::Value::Array group_values;
        group_values.reserve(groups.size());
        for (std::size_t index = 0; index < groups.size(); ++index) {
            group_values.push_back(parameter_group_value(groups[index], index));
        }

        json::Value::Object payload;
        payload.emplace("count", number_value(definitions.size()));
        payload.emplace("parameter_count", number_value(definitions.size()));
        payload.emplace("group_count", number_value(groups.size()));
        payload.emplace("parameter_revision", number_value(
            static_cast<double>(skeleton.parameter_revision())));
        payload.emplace("project_revision", number_value(
            static_cast<double>(session.project_revision())));
        payload.emplace("runtime_revision", number_value(
            static_cast<double>(session.runtime_revision())));
        payload.emplace("preview_revision", number_value(
            static_cast<double>(session.preview_revision())));
        payload.emplace("undo_count", number_value(session.undo_count()));
        payload.emplace("redo_count", number_value(session.redo_count()));
        payload.emplace("project_dirty", bool_value(session.dirty()));
        payload.emplace("definitions", array_value(std::move(definition_values)));
        payload.emplace("groups", array_value(std::move(group_values)));
        payload.emplace("direct_values", object_value(std::move(direct_by_id)));
        payload.emplace("final_values", object_value(std::move(final_by_id)));
        return make_success(
            "Listed parameter definitions and preview values.",
            op,
            spec,
            object_value(std::move(payload)));
    }

    const json::Value* args = command_args(cmd);
    if (args == nullptr) {
        return make_error(std::string(op) + " requires an 'args' object.", op, spec);
    }

    if (op == "parameter.set") {
        std::string unexpected;
        if (!args_contain_only(*args, {"id", "value", "dry_run"}, &unexpected)) {
            return make_error("Unexpected parameter.set argument: " + unexpected, op, spec);
        }
        const auto id = string_arg(*args, "id");
        const auto requested = number_arg(*args, "value");
        if (!id.has_value() || id->empty() || !requested.has_value() ||
            !std::isfinite(*requested)) {
            return make_error(
                "parameter.set requires a non-empty id and finite value.", op, spec);
        }
        bool dry_run = false;
        std::string bool_error;
        if (!optional_bool_arg(*args, "dry_run", false, &dry_run, &bool_error)) {
            return make_error(std::move(bool_error), op, spec);
        }

        const runtime::SkeletonData& data = *session.runtime_data();
        const auto parameter_index = data.find_parameter_index(*id);
        if (!parameter_index.has_value()) {
            return make_error(
                "Parameter not found: " + std::string(*id), op, spec, "not_found");
        }
        const runtime::ParameterDefinition& definition =
            data.parameters()[*parameter_index];
        double normalized = definition.type == runtime::ParameterType::Discrete
            ? std::round(*requested)
            : *requested;
        const double unclamped = normalized;
        if (definition.clamp) {
            normalized = std::clamp(normalized, definition.min_value, definition.max_value);
        }
        const bool clamped = normalized != unclamped;

        if (dry_run) {
            // parameter.set only mutates preview state; rebuilding a runtime
            // from an unmodified project copy validated nothing about the
            // requested value, so the preview evaluation below is the whole
            // dry-run contract.
            const std::optional<double> applied =
                session.evaluate_preview_parameter_value(*id, *requested);
            if (!applied.has_value()) {
                return make_error(
                    "Failed to evaluate parameter against the copied preview state.",
                    op,
                    spec,
                    "preview_unavailable");
            }
            return make_success(
                "Parameter preview edit validated.",
                op,
                spec,
                parameter_result_value(
                    *id,
                    *requested,
                    *applied,
                    clamped,
                    true));
        }

        const SessionResult result = session.set_preview_parameter_value(
            std::string(*id),
            *requested,
            {EditKind::PreviewComposition,
             "Set parameter via Agent",
             "Agent",
             false,
             EditImpact::Preview});
        if (!result) {
            return make_error(
                "Failed to set parameter preview: " + result.error->format(), op, spec);
        }
        const runtime::Skeleton& preview = *session.preview_skeleton();
        return make_success(
            "Set parameter preview successfully.",
            op,
            spec,
            parameter_result_value(
                *id,
                *requested,
                preview.parameter_values()[*parameter_index],
                clamped,
                false));
    }

    if (op == "deformer.create") {
        std::string unexpected;
        if (!args_contain_only(*args, {"deformer", "dry_run"}, &unexpected)) {
            return make_error("Unexpected deformer.create argument: " + unexpected, op, spec);
        }
        const json::Value* definition = json::find_member(*args, "deformer");
        if (definition == nullptr || !definition->is_object()) {
            return make_error("deformer.create requires a deformer object.", op, spec);
        }
        bool dry_run = false;
        std::string bool_error;
        if (!optional_bool_arg(*args, "dry_run", false, &dry_run, &bool_error)) {
            return make_error(std::move(bool_error), op, spec);
        }
        const json::Value* id_value = json::find_member(*definition, "id");
        const std::string id = id_value != nullptr && id_value->is_string()
            ? id_value->as_string()
            : std::string{};
        if (dry_run) {
            ProjectData candidate = *session.project();
            const AuthoringResult result =
                upsert_parameter_deformer(&candidate, *definition, false);
            json::Value::Object payload;
            payload.emplace("id", string_value(id));
            return validate_candidate(
                std::move(candidate),
                session,
                result,
                op,
                spec,
                "Parameter deformer creation validated.",
                std::move(payload));
        }
        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Create parameter deformer via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        const AuthoringResult result =
            upsert_parameter_deformer(transaction.project(), *definition, false);
        json::Value::Object payload;
        payload.emplace("id", string_value(id));
        return commit_authoring(
            op,
            spec,
            "create parameter deformer",
            result,
            &transaction,
            "Created parameter deformer successfully.",
            std::move(payload));
    }

    if (op == "keyform.capture") {
        std::string unexpected;
        if (!args_contain_only(
                *args, {"deformer", "replace", "dry_run"}, &unexpected)) {
            return make_error("Unexpected keyform.capture argument: " + unexpected, op, spec);
        }
        const auto deformer_id = string_arg(*args, "deformer");
        if (!deformer_id.has_value() || deformer_id->empty()) {
            return make_error(
                "keyform.capture requires a non-empty deformer id.", op, spec);
        }
        bool replace = false;
        bool dry_run = false;
        std::string bool_error;
        if (!optional_bool_arg(*args, "replace", false, &replace, &bool_error) ||
            !optional_bool_arg(*args, "dry_run", false, &dry_run, &bool_error)) {
            return make_error(std::move(bool_error), op, spec);
        }
        if (dry_run) {
            ProjectData candidate = *session.project();
            const AuthoringResult result = capture_current_deformer_keyform(
                &candidate,
                *session.runtime_data(),
                *session.preview_skeleton(),
                *deformer_id,
                replace);
            json::Value::Object payload;
            payload.emplace("deformer", string_value(std::string(*deformer_id)));
            payload.emplace("replace", bool_value(replace));
            return validate_candidate(
                std::move(candidate),
                session,
                result,
                op,
                spec,
                "Deformer keyform capture validated.",
                std::move(payload));
        }
        auto transaction = session.begin_edit({
            EditKind::AddKeyframe,
            "Capture parameter keyform via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        const AuthoringResult result = capture_current_deformer_keyform(
            transaction.project(),
            *session.runtime_data(),
            *session.preview_skeleton(),
            *deformer_id,
            replace);
        json::Value::Object payload;
        payload.emplace("deformer", string_value(std::string(*deformer_id)));
        payload.emplace("replace", bool_value(replace));
        return commit_authoring(
            op,
            spec,
            "capture parameter keyform",
            result,
            &transaction,
            "Captured parameter keyform successfully.",
            std::move(payload));
    }

    if (op == "expression.create") {
        std::string unexpected;
        if (!args_contain_only(*args, {"expression", "dry_run"}, &unexpected)) {
            return make_error("Unexpected expression.create argument: " + unexpected, op, spec);
        }
        const json::Value* definition = json::find_member(*args, "expression");
        if (definition == nullptr || !definition->is_object()) {
            return make_error("expression.create requires an expression object.", op, spec);
        }
        bool dry_run = false;
        std::string bool_error;
        if (!optional_bool_arg(*args, "dry_run", false, &dry_run, &bool_error)) {
            return make_error(std::move(bool_error), op, spec);
        }
        const json::Value* id_value = json::find_member(*definition, "id");
        const std::string id = id_value != nullptr && id_value->is_string()
            ? id_value->as_string()
            : std::string{};
        if (dry_run) {
            ProjectData candidate = *session.project();
            const AuthoringResult result = upsert_expression(&candidate, *definition, false);
            json::Value::Object payload;
            payload.emplace("id", string_value(id));
            return validate_candidate(
                std::move(candidate),
                session,
                result,
                op,
                spec,
                "Expression creation validated.",
                std::move(payload));
        }
        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Create expression via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        const AuthoringResult result = upsert_expression(
            transaction.project(), *definition, false);
        json::Value::Object payload;
        payload.emplace("id", string_value(id));
        return commit_authoring(
            op,
            spec,
            "create expression",
            result,
            &transaction,
            "Created expression successfully.",
            std::move(payload));
    }

    if (op == "lip_sync.map") {
        std::string unexpected;
        if (!args_contain_only(*args, {"mapping", "dry_run"}, &unexpected)) {
            return make_error("Unexpected lip_sync.map argument: " + unexpected, op, spec);
        }
        const json::Value* mapping = json::find_member(*args, "mapping");
        if (mapping == nullptr || !mapping->is_object()) {
            return make_error("lip_sync.map requires a mapping object.", op, spec);
        }
        bool dry_run = false;
        std::string bool_error;
        if (!optional_bool_arg(*args, "dry_run", false, &dry_run, &bool_error)) {
            return make_error(std::move(bool_error), op, spec);
        }
        const json::Value* parameter_value = json::find_member(*mapping, "parameter");
        const std::string parameter =
            parameter_value != nullptr && parameter_value->is_string()
            ? parameter_value->as_string()
            : std::string{};
        if (dry_run) {
            ProjectData candidate = *session.project();
            const AuthoringResult result = upsert_lip_sync_mapping(&candidate, *mapping);
            json::Value::Object payload;
            payload.emplace("parameter", string_value(parameter));
            return validate_candidate(
                std::move(candidate),
                session,
                result,
                op,
                spec,
                "Lip-sync mapping validated.",
                std::move(payload));
        }
        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Map lip sync via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        const AuthoringResult result =
            upsert_lip_sync_mapping(transaction.project(), *mapping);
        json::Value::Object payload;
        payload.emplace("parameter", string_value(parameter));
        return commit_authoring(
            op,
            spec,
            "map lip sync",
            result,
            &transaction,
            "Mapped lip-sync input successfully.",
            std::move(payload));
    }

    return make_error(
        "Unknown operation: " + std::string(op), op, spec, "unknown_operation");
}

} // namespace marrow::editor::agent_detail
