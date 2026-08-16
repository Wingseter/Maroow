#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "marrow/editor/authoring.hpp"
#include "shell_state.hpp"

namespace marrow::editor::shell::parameter_panel {

using marrow::editor::AuthoringResult;
using marrow::runtime::json::Value;

Value string_value(std::string value);
Value number_value(double value);
Value boolean_value(bool value);
Value array_value(Value::Array values = {});
Value object_value(Value::Object values = {});

const std::string* raw_id(const Value& value);

bool set_parameter_geometry_value(
    marrow::editor::ProjectData* project,
    std::string_view deformer_id,
    std::string_view field,
    double value);

std::optional<std::size_t> current_deformer_keyform_index(
    const ShellState& state,
    std::string_view deformer_id,
    const marrow::editor::ParameterDeformerAuthoringDefinition& authored_deformer);

void resize_warp_grid(
    marrow::editor::ParameterDeformerAuthoringDefinition* deformer,
    std::size_t columns,
    std::size_t rows);

std::string authoring_error(const AuthoringResult& result);

template <typename Mutate>
bool apply_persistent_parameter_edit(
    ShellState* state,
    std::string label,
    Mutate&& mutate) {
    if (state == nullptr || !state->session.has_project() ||
        authoring_gesture_active(*state) || state->session.transaction_active()) {
        return false;
    }
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        std::move(label),
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }
    const AuthoringResult authored = mutate(transaction.project());
    if (!authored) {
        state->error_message = authoring_error(authored);
        transaction.cancel();
        return false;
    }
    const marrow::editor::SessionResult committed = transaction.commit();
    sync_shell_from_editor_session(state);
    if (!committed) {
        state->error_message = committed.error->format();
        return false;
    }
    state->error_message.clear();
    state->status_message = "Updated Parameter Model";
    return true;
}

bool apply_parameter_shape_definition_edit(
    ShellState* state,
    std::string label,
    marrow::editor::ParameterShapeAuthoringDefinition definition);

bool apply_parameter_deformer_definition_edit(
    ShellState* state,
    std::string label,
    marrow::editor::ParameterDeformerAuthoringDefinition definition);

bool apply_expression_definition_edit(
    ShellState* state,
    std::string label,
    marrow::editor::ExpressionAuthoringDefinition definition);

std::optional<Value> make_default_rotation_deformer(
    ShellState* state,
    std::string* error_out);

std::optional<Value> make_default_warp_deformer(
    ShellState* state,
    std::string* error_out);

Value make_default_expression(const marrow::editor::ParameterModel& model);

Value make_default_lip_mapping(
    const marrow::editor::ParameterAuthoringDefinition& parameter);

Value build_lip_mapping_value(
    const marrow::editor::LipSyncMappingAuthoringDefinition& mapping);

bool apply_lip_sync_mapping_definition_edit(
    ShellState* state,
    std::string label,
    std::string previous_parameter,
    marrow::editor::LipSyncMappingAuthoringDefinition definition);

} // namespace marrow::editor::shell::parameter_panel
