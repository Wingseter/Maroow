#include "shell_parameters.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "marrow/editor/authoring.hpp"
#include "shell_project_panels.hpp"

namespace marrow::editor::shell {
namespace {

using marrow::editor::AuthoringResult;
using marrow::editor::ParameterAuthoringDefinition;
using marrow::editor::ParameterAuthoringType;
using marrow::editor::ParameterGroupAuthoringDefinition;
using marrow::runtime::json::Value;

struct ParameterPanelSelection {
    std::string parameter_id;
    std::string group_id;
    std::string shape_id;
    std::string deformer_id;
    std::string expression_id;
    std::string lip_parameter_id;
};

ParameterPanelSelection g_selection;
std::string g_pending_capture_id;

struct PendingMissingGeometryCapture {
    std::string deformer_id;
    std::string field;
    double value{0.0};
};

std::optional<PendingMissingGeometryCapture> g_pending_missing_geometry_capture;

std::optional<std::size_t> current_deformer_keyform_index(
    const ShellState& state,
    std::string_view deformer_id,
    const marrow::editor::ParameterDeformerAuthoringDefinition& authored_deformer);

Value string_value(std::string value) { return Value(std::move(value), {}); }
Value number_value(double value) { return Value(value, {}); }
Value boolean_value(bool value) { return Value(value, {}); }
Value array_value(Value::Array values = {}) { return Value(std::move(values), {}); }
Value object_value(Value::Object values = {}) { return Value(std::move(values), {}); }

bool input_string_field(const char* label, std::string* value) {
    std::array<char, 256> buffer{};
    const std::size_t length = std::min(value->size(), buffer.size() - 1U);
    std::copy_n(value->data(), length, buffer.data());
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
    *value = buffer.data();
    return true;
}

const std::string* raw_id(const Value& value) {
    const Value* id = marrow::runtime::json::find_member(value, "id");
    return value.is_object() && id != nullptr && id->is_string()
        ? &id->as_string()
        : nullptr;
}

bool set_parameter_geometry_value(
    marrow::editor::ProjectData* project,
    std::string_view deformer_id,
    std::string_view field,
    double value) {
    if (project == nullptr || !project->parameter_model.has_value() ||
        !std::isfinite(value)) {
        return false;
    }
    marrow::editor::ParameterDeformerAuthoringDefinition* deformer =
        project->parameter_model->find_deformer(deformer_id);
    if (deformer == nullptr) return false;
    if (field == "pivot:x" || field == "pivot:y") {
        if (deformer->kind != marrow::runtime::ParameterDeformerKind::Rotation) return false;
        if (field == "pivot:x") deformer->pivot.x = static_cast<float>(value);
        else deformer->pivot.y = static_cast<float>(value);
        return true;
    }
    constexpr std::string_view prefix = "keyform:";
    if (field.substr(0U, prefix.size()) != prefix) return false;
    const std::size_t separator = field.find(':', prefix.size());
    if (separator == std::string_view::npos) return false;
    std::size_t keyform_index = 0U;
    std::size_t component_index = 0U;
    try {
        keyform_index = static_cast<std::size_t>(std::stoull(
            std::string(field.substr(prefix.size(), separator - prefix.size()))));
        component_index = static_cast<std::size_t>(std::stoull(
            std::string(field.substr(separator + 1U))));
    } catch (...) {
        return false;
    }
    if (deformer->kind != marrow::runtime::ParameterDeformerKind::Warp ||
        keyform_index >= deformer->warp_keyforms.size()) {
        return false;
    }
    auto& control_points = deformer->warp_keyforms[keyform_index].control_points;
    if (component_index >= control_points.size() * 2U) {
        return false;
    }
    marrow::runtime::AttachmentVertex& point = control_points[component_index / 2U];
    if (component_index % 2U == 0U) point.x = static_cast<float>(value);
    else point.y = static_cast<float>(value);
    return true;
}

std::string unique_id(
    const marrow::editor::ParameterModel& model,
    std::string_view stem) {
    for (std::size_t suffix = 1U;; ++suffix) {
        const std::string candidate =
            std::string(stem) + "." + std::to_string(suffix);
        const bool typed_exists = model.find_parameter(candidate) != nullptr ||
            model.find_group(candidate) != nullptr ||
            model.find_shape(candidate) != nullptr ||
            model.find_deformer(candidate) != nullptr ||
            model.find_art_path(candidate) != nullptr ||
            model.find_expression(candidate) != nullptr;
        if (!typed_exists) {
            return candidate;
        }
    }
}

std::string authoring_error(const AuthoringResult& result) {
    std::string message = result.error;
    if (!result.dependencies.empty()) {
        message += " Dependencies: ";
        for (std::size_t index = 0U; index < result.dependencies.size(); ++index) {
            if (index != 0U) message += ", ";
            message += result.dependencies[index];
        }
    }
    return message;
}

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
        // Commit always builds the candidate runtime before adopting project,
        // runtime, and preview snapshots, and rolls all three back on failure.
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
    marrow::editor::ParameterShapeAuthoringDefinition definition) {
    Value value = marrow::editor::build_parameter_shape_authoring_value(definition);
    return apply_persistent_parameter_edit(
        state,
        std::move(label),
        [value = std::move(value)](marrow::editor::ProjectData* project) mutable {
            return marrow::editor::upsert_parameter_shape(
                project, std::move(value), true);
        });
}

bool apply_parameter_deformer_definition_edit(
    ShellState* state,
    std::string label,
    marrow::editor::ParameterDeformerAuthoringDefinition definition) {
    Value value = marrow::editor::build_parameter_deformer_authoring_value(definition);
    return apply_persistent_parameter_edit(
        state,
        std::move(label),
        [value = std::move(value)](marrow::editor::ProjectData* project) mutable {
            return marrow::editor::upsert_parameter_deformer(
                project, std::move(value), true);
        });
}

bool apply_expression_definition_edit(
    ShellState* state,
    std::string label,
    marrow::editor::ExpressionAuthoringDefinition definition) {
    Value value = marrow::editor::build_expression_authoring_value(definition);
    return apply_persistent_parameter_edit(
        state,
        std::move(label),
        [value = std::move(value)](marrow::editor::ProjectData* project) mutable {
            return marrow::editor::upsert_expression(
                project, std::move(value), true);
        });
}

const char* parameter_deformer_axis_name(
    marrow::runtime::ParameterDeformerAxis axis) {
    switch (axis) {
    case marrow::runtime::ParameterDeformerAxis::X:
        return "x";
    case marrow::runtime::ParameterDeformerAxis::Y:
        return "y";
    case marrow::runtime::ParameterDeformerAxis::Angle:
        return "angle";
    }
    return "unknown";
}

bool capture_current_keyform(
    ShellState* state,
    std::string id,
    bool replace_existing) {
    return apply_persistent_parameter_edit(
        state,
        (replace_existing ? "Replace parameter keyform " : "Capture parameter keyform ") + id,
        [state, id = std::move(id), replace_existing](
            marrow::editor::ProjectData* candidate) {
            return marrow::editor::capture_current_deformer_keyform(
                candidate,
                *state->load_result.skeleton_data,
                *state->preview_skeleton,
                id,
                replace_existing);
        });
}

void draw_capture_replace_popup(ShellState* state) {
    if (!ImGui::BeginPopupModal(
            "Replace Parameter Keyform?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    ImGui::TextWrapped(
        "A keyform already exists at the current parameter coordinates for '%s'.",
        g_pending_capture_id.c_str());
    ImGui::TextDisabled("Replace is one undoable Project | Runtime | Preview transaction.");
    if (ImGui::Button("Replace")) {
        if (capture_current_keyform(state, g_pending_capture_id, true)) {
            g_pending_capture_id.clear();
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        g_pending_capture_id.clear();
        state->error_message.clear();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void draw_missing_geometry_capture_popup(ShellState* state) {
    if (!ImGui::BeginPopupModal(
            "Capture Missing Geometry Keyform?",
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }
    if (!g_pending_missing_geometry_capture.has_value()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    auto& pending = *g_pending_missing_geometry_capture;
    ImGui::TextWrapped(
        "No keyform exists at the current parameter coordinates for '%s'.",
        pending.deformer_id.c_str());
    ImGui::TextDisabled(
        "Capture and the first geometry edit will be one Project | Runtime | Preview transaction.");
    ImGui::InputDouble("Edited Value", &pending.value, 0.25, 1.0);
    if (ImGui::Button("Capture and Apply")) {
        auto transaction = state->session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "Capture and edit parameter geometry " + pending.deformer_id,
            {},
            false,
            marrow::editor::EditImpact::Project |
                marrow::editor::EditImpact::Runtime |
                marrow::editor::EditImpact::Preview});
        if (!transaction) {
            state->error_message = transaction.error()->format();
        } else {
            AuthoringResult captured = marrow::editor::capture_current_deformer_keyform(
                transaction.project(),
                *state->load_result.skeleton_data,
                *state->preview_skeleton,
                pending.deformer_id,
                false);
            std::string field = pending.field;
            if (captured && field == "warp:first-x") {
                const auto* candidate_deformer =
                    transaction.project()->parameter_model->find_deformer(
                        pending.deformer_id);
                const auto current = candidate_deformer == nullptr
                    ? std::nullopt
                    : current_deformer_keyform_index(
                          *state, pending.deformer_id, *candidate_deformer);
                if (!current.has_value()) {
                    captured = {false, "Captured warp keyform could not be resolved."};
                } else {
                    field = "keyform:" + std::to_string(*current) + ":0";
                }
            }
            if (!captured || !set_parameter_geometry_value(
                    transaction.project(),
                    pending.deformer_id,
                    field,
                    pending.value)) {
                state->error_message = captured
                    ? "Could not apply the captured geometry edit."
                    : authoring_error(captured);
                transaction.cancel();
            } else {
                const auto refreshed = transaction.refresh_runtime();
                marrow::editor::SessionResult committed = refreshed;
                if (refreshed) committed = transaction.commit();
                else transaction.cancel();
                sync_shell_from_editor_session(state);
                if (!committed) {
                    state->error_message = committed.error->format();
                } else {
                    state->error_message.clear();
                    state->status_message = "Captured and edited parameter geometry";
                    g_pending_missing_geometry_capture.reset();
                    ImGui::CloseCurrentPopup();
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        g_pending_missing_geometry_capture.reset();
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void finish_parameter_slider_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->parameter_slider_gesture.has_value()) return;
    ParameterSliderGesture gesture = std::move(*state->parameter_slider_gesture);
    state->parameter_slider_gesture.reset();
    if (!commit || !gesture.changed) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        return;
    }
    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
    } else {
        state->error_message.clear();
        state->status_message = "Previewed " + gesture.parameter_id;
    }
}

void finish_parameter_geometry_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->parameter_geometry_gesture.has_value()) return;
    ParameterGeometryGesture gesture = std::move(*state->parameter_geometry_gesture);
    state->parameter_geometry_gesture.reset();
    if (!commit || !gesture.changed) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        return;
    }
    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
    } else {
        state->error_message.clear();
        state->status_message = "Edited parameter geometry " + gesture.deformer_id;
    }
}

bool draw_parameter_geometry_scalar(
    ShellState* state,
    std::string_view deformer_id,
    std::string field,
    const char* label,
    double value) {
    ImGui::PushID(field.c_str());
    const bool changed = ImGui::DragScalar(
        label,
        ImGuiDataType_Double,
        &value,
        0.25f,
        nullptr,
        nullptr,
        "%.3f");
    if (ImGui::IsItemActivated() && !state->parameter_geometry_gesture.has_value()) {
        auto transaction = state->session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "Edit parameter geometry " + std::string(deformer_id),
            {},
            false,
            marrow::editor::EditImpact::Project |
                marrow::editor::EditImpact::Runtime |
                marrow::editor::EditImpact::Preview});
        if (transaction) {
            state->parameter_geometry_gesture = ParameterGeometryGesture{
                std::string(deformer_id), field, false, std::move(transaction)};
        } else {
            state->error_message = transaction.error()->format();
        }
    }
    if (changed && state->parameter_geometry_gesture.has_value() &&
        state->parameter_geometry_gesture->deformer_id == deformer_id &&
        state->parameter_geometry_gesture->field == field) {
        if (!set_parameter_geometry_value(
                state->parameter_geometry_gesture->transaction.project(),
                deformer_id,
                field,
                value)) {
            state->error_message = "Failed to edit the selected parameter geometry.";
            finish_parameter_geometry_gesture(state, false);
        } else {
            const marrow::editor::SessionResult refreshed =
                state->parameter_geometry_gesture->transaction.refresh_runtime();
            if (!refreshed) {
                state->error_message = refreshed.error->format();
                finish_parameter_geometry_gesture(state, false);
            } else {
                state->parameter_geometry_gesture->changed = true;
                sync_shell_from_editor_session(state);
            }
        }
    }
    const bool deactivated = ImGui::IsItemDeactivated();
    ImGui::PopID();
    if (deactivated) {
        finish_parameter_geometry_gesture(state, true);
    }
    return changed;
}

void draw_preview_parameter_slider(
    ShellState* state,
    const ParameterAuthoringDefinition& parameter) {
    const auto current = state->session.preview_state().direct_parameter_values.find(parameter.id);
    double value = current == state->session.preview_state().direct_parameter_values.end()
        ? parameter.default_value
        : current->second;
    ImGui::PushID(parameter.id.c_str());
    const char* format = parameter.type == ParameterAuthoringType::Discrete ? "%.0f" : "%.3f";
    const bool changed = ImGui::SliderScalar(
        parameter.name.c_str(),
        ImGuiDataType_Double,
        &value,
        &parameter.min_value,
        &parameter.max_value,
        format,
        ImGuiSliderFlags_AlwaysClamp);
    if (ImGui::IsItemActivated() && !state->parameter_slider_gesture.has_value()) {
        auto transaction = state->session.begin_edit({
            marrow::editor::EditKind::PreviewComposition,
            "Change preview parameter " + parameter.name,
            "preview-parameter:" + parameter.id,
            false,
            marrow::editor::EditImpact::Preview});
        if (transaction) {
            state->parameter_slider_gesture = ParameterSliderGesture{
                parameter.id, false, std::move(transaction)};
        } else {
            state->error_message = transaction.error()->format();
        }
    }
    if (changed && state->parameter_slider_gesture.has_value() &&
        state->parameter_slider_gesture->parameter_id == parameter.id) {
        if (state->parameter_slider_gesture->transaction.set_preview_parameter_value(
                parameter.id, value)) {
            state->parameter_slider_gesture->changed = true;
        } else {
            state->error_message =
                state->parameter_slider_gesture->transaction.error()->format();
            finish_parameter_slider_gesture(state, false);
        }
    }
    if (ImGui::IsItemDeactivated()) {
        finish_parameter_slider_gesture(state, true);
    }
    double numeric_value = state->session.preview_state().direct_parameter_values.at(parameter.id);
    ImGui::BeginDisabled(state->parameter_slider_gesture.has_value());
    if (ImGui::InputDouble("Numeric", &numeric_value, 0.0, 0.0, format)) {
        const auto result = state->session.set_preview_parameter_value(
            parameter.id,
            numeric_value,
            {marrow::editor::EditKind::PreviewComposition,
             "Enter preview parameter " + parameter.name,
             "preview-parameter:" + parameter.id,
             true,
             marrow::editor::EditImpact::Preview});
        sync_shell_from_editor_session(state);
        if (!result) state->error_message = result.error->format();
    }
    ImGui::EndDisabled();
    const auto parameter_index = state->load_result.skeleton_data != nullptr
        ? state->load_result.skeleton_data->find_parameter_index(parameter.id)
        : std::nullopt;
    if (parameter_index.has_value() && state->preview_skeleton != nullptr &&
        *parameter_index < state->preview_skeleton->parameter_values().size()) {
        ImGui::SameLine();
        ImGui::TextDisabled(
            "final %.3f",
            state->preview_skeleton->parameter_values()[*parameter_index]);
    }
    ImGui::PopID();
}

void draw_parameters_window(ShellState* state) {
    if (!ImGui::Begin(kParametersWindowTitle)) {
        ImGui::End();
        return;
    }
    const marrow::editor::ProjectData* project = state->session.project();
    if (project == nullptr) {
        ImGui::TextDisabled("Open a project to author parameters.");
        ImGui::End();
        return;
    }
    static const marrow::editor::ParameterModel kEmptyModel;
    const auto& model = project->parameter_model.value_or(kEmptyModel);
    const bool edit_blocked = authoring_gesture_active(*state) ||
        state->session.transaction_active();

    ImGui::BeginDisabled(edit_blocked);
    if (ImGui::Button("Add Parameter")) {
        ParameterAuthoringDefinition definition;
        definition.id = unique_id(model, "parameter");
        definition.name = "Parameter " + std::to_string(model.parameters.size() + 1U);
        if (apply_persistent_parameter_edit(
                state,
                "Create parameter " + definition.id,
                [definition](marrow::editor::ProjectData* candidate) mutable {
                    return marrow::editor::create_parameter(candidate, std::move(definition));
                })) {
            g_selection.parameter_id = definition.id;
            ImGui::EndDisabled();
            ImGui::End();
            return;
        }
    }
    ImGui::EndDisabled();

    project = state->session.project();
    const auto* live_model = project != nullptr && project->parameter_model.has_value()
        ? &*project->parameter_model
        : nullptr;
    if (live_model == nullptr || live_model->parameters.empty()) {
        ImGui::TextDisabled("No parameters yet.");
    } else {
        for (const ParameterAuthoringDefinition& parameter : live_model->parameters) {
            if (ImGui::Selectable(
                    (parameter.name + "##" + parameter.id).c_str(),
                    g_selection.parameter_id == parameter.id)) {
                g_selection.parameter_id = parameter.id;
            }
            draw_preview_parameter_slider(state, parameter);
        }
    }

    project = state->session.project();
    live_model = project != nullptr && project->parameter_model.has_value()
        ? &*project->parameter_model
        : nullptr;
    const ParameterAuthoringDefinition* selected = live_model == nullptr
        ? nullptr
        : live_model->find_parameter(g_selection.parameter_id);
    if (selected != nullptr) {
        ImGui::SeparatorText("Definition");
        ParameterAuthoringDefinition edited = *selected;
        bool discrete = edited.type == ParameterAuthoringType::Discrete;
        bool persistent_change = input_string_field("Name", &edited.name);
        persistent_change |= ImGui::InputDouble("Minimum", &edited.min_value, 0.01, 0.1);
        persistent_change |= ImGui::InputDouble("Maximum", &edited.max_value, 0.01, 0.1);
        persistent_change |= ImGui::Checkbox("Discrete", &discrete);
        ImGui::SameLine();
        persistent_change |= ImGui::Checkbox("Clamp", &edited.clamp);
        persistent_change |= ImGui::InputDouble("Default", &edited.default_value, 0.01, 0.1);
        bool has_ui_step = edited.ui_step.has_value();
        if (ImGui::Checkbox("Custom UI Step", &has_ui_step)) {
            edited.ui_step = has_ui_step ? std::optional<double>(0.01) : std::nullopt;
            persistent_change = true;
        }
        if (edited.ui_step.has_value()) {
            double ui_step = *edited.ui_step;
            if (ImGui::InputDouble("UI Step", &ui_step, 0.01, 0.1)) {
                edited.ui_step = ui_step;
                persistent_change = true;
            }
        }
        std::string units = edited.units.value_or("");
        if (input_string_field("Units", &units)) {
            edited.units = units.empty()
                ? std::nullopt
                : std::optional<std::string>(std::move(units));
            persistent_change = true;
        }
        if (persistent_change) {
            edited.type = discrete ? ParameterAuthoringType::Discrete
                                   : ParameterAuthoringType::Continuous;
            if (edited.clamp) {
                edited.default_value = std::clamp(
                    edited.default_value, edited.min_value, edited.max_value);
            }
            const std::string id = edited.id;
            (void)apply_persistent_parameter_edit(
                state,
                "Edit parameter " + id,
                [id, edited](marrow::editor::ProjectData* candidate) mutable {
                    return marrow::editor::update_parameter(candidate, id, std::move(edited));
                });
            ImGui::End();
            return;
        }
        ImGui::BeginDisabled(edit_blocked);
        if (ImGui::Button("Delete Parameter")) {
            const std::string id = selected->id;
            if (apply_persistent_parameter_edit(
                    state,
                    "Delete parameter " + id,
                    [id](marrow::editor::ProjectData* candidate) {
                        return marrow::editor::delete_parameter(candidate, id);
                    })) {
                g_selection.parameter_id.clear();
                ImGui::EndDisabled();
                ImGui::End();
                return;
            }
        }
        ImGui::EndDisabled();
    }

    ImGui::SeparatorText("Groups");
    project = state->session.project();
    live_model = project != nullptr && project->parameter_model.has_value()
        ? &*project->parameter_model
        : nullptr;
    ImGui::BeginDisabled(edit_blocked);
    if (ImGui::Button("Add Group")) {
        ParameterGroupAuthoringDefinition group;
        group.id = unique_id(live_model == nullptr ? kEmptyModel : *live_model, "group");
        group.name = "Group";
        const std::string id = group.id;
        if (apply_persistent_parameter_edit(
                state,
                "Create parameter group " + id,
                [group](marrow::editor::ProjectData* candidate) mutable {
                    return marrow::editor::create_parameter_group(candidate, std::move(group));
                })) {
            g_selection.group_id = id;
            ImGui::EndDisabled();
            ImGui::End();
            return;
        }
    }
    ImGui::EndDisabled();
    if (live_model != nullptr) {
        for (const ParameterGroupAuthoringDefinition& group : live_model->groups) {
            if (ImGui::Selectable(
                    (group.name + "##" + group.id).c_str(),
                    g_selection.group_id == group.id)) {
                g_selection.group_id = group.id;
            }
        }
        const ParameterGroupAuthoringDefinition* group =
            live_model->find_group(g_selection.group_id);
        if (group != nullptr) {
            ParameterGroupAuthoringDefinition group_edit = *group;
            bool group_changed = input_string_field("Group Name", &group_edit.name);
            group_changed |= ImGui::Checkbox("Collapsed", &group_edit.collapsed);
            std::string color_tag = group_edit.color_tag.value_or("");
            if (input_string_field("Color Tag", &color_tag)) {
                group_edit.color_tag = color_tag.empty()
                    ? std::nullopt
                    : std::optional<std::string>(std::move(color_tag));
                group_changed = true;
            }
            std::string exclusive_mode = group_edit.exclusive_mode.value_or("");
            if (input_string_field("Exclusive Mode", &exclusive_mode)) {
                group_edit.exclusive_mode = exclusive_mode.empty()
                    ? std::nullopt
                    : std::optional<std::string>(std::move(exclusive_mode));
                group_changed = true;
            }
            if (group_changed) {
                const std::string id = group_edit.id;
                (void)apply_persistent_parameter_edit(
                    state,
                    "Edit parameter group " + id,
                    [id, group_edit](marrow::editor::ProjectData* candidate) mutable {
                        return marrow::editor::update_parameter_group(
                            candidate, id, std::move(group_edit));
                    });
                ImGui::End();
                return;
            }
            for (const ParameterAuthoringDefinition& parameter : live_model->parameters) {
                bool included = std::find(
                    group->parameter_ids.begin(), group->parameter_ids.end(), parameter.id) !=
                    group->parameter_ids.end();
                if (ImGui::Checkbox(
                        (parameter.name + "##group:" + parameter.id).c_str(), &included)) {
                    ParameterGroupAuthoringDefinition edited = *group;
                    auto found = std::find(
                        edited.parameter_ids.begin(), edited.parameter_ids.end(), parameter.id);
                    if (included && found == edited.parameter_ids.end()) {
                        edited.parameter_ids.push_back(parameter.id);
                    } else if (!included && found != edited.parameter_ids.end()) {
                        edited.parameter_ids.erase(found);
                    }
                    const std::string id = edited.id;
                    (void)apply_persistent_parameter_edit(
                        state,
                        "Edit parameter group " + id,
                        [id, edited](marrow::editor::ProjectData* candidate) mutable {
                            return marrow::editor::update_parameter_group(
                                candidate, id, std::move(edited));
                        });
                    break;
                }
            }
            ImGui::BeginDisabled(edit_blocked);
            if (ImGui::Button("Delete Group")) {
                const std::string id = group->id;
                if (apply_persistent_parameter_edit(
                        state,
                        "Delete parameter group " + id,
                        [id](marrow::editor::ProjectData* candidate) {
                            return marrow::editor::delete_parameter_group(candidate, id);
                        })) {
                    g_selection.group_id.clear();
                }
            }
            ImGui::EndDisabled();
        }
    }
    ImGui::End();
}

std::optional<Value> make_default_shape(ShellState* state, std::string* error_out) {
    const auto* data = state->load_result.skeleton_data.get();
    const auto* project = state->session.project();
    if (data == nullptr || project == nullptr || !project->parameter_model.has_value()) {
        *error_out = "Create a continuous parameter first.";
        return std::nullopt;
    }
    const auto parameter = std::find_if(
        project->parameter_model->parameters.begin(),
        project->parameter_model->parameters.end(),
        [](const ParameterAuthoringDefinition& candidate) {
            return candidate.type == ParameterAuthoringType::Continuous &&
                candidate.min_value < candidate.max_value;
        });
    if (parameter == project->parameter_model->parameters.end()) {
        *error_out = "A non-degenerate continuous parameter is required.";
        return std::nullopt;
    }
    std::size_t slot_index = 0U;
    const marrow::runtime::AttachmentData* attachment = nullptr;
    for (; slot_index < data->slots().size(); ++slot_index) {
        attachment = data->find_attachment_source(
            slot_index, data->slots()[slot_index].setup_attachment);
        if (attachment != nullptr && attachment->mesh_geometry != nullptr) break;
    }
    if (slot_index >= data->slots().size() || attachment == nullptr) {
        *error_out = "A setup-pose mesh attachment is required.";
        return std::nullopt;
    }
    const std::size_t component_count = attachment->mesh_geometry->vertices.size();
    Value::Array zero_vertices(component_count, number_value(0.0));
    Value::Array keyforms;
    keyforms.push_back(object_value({
        {"value", number_value(parameter->min_value)},
        {"vertices", array_value(zero_vertices)},
    }));
    keyforms.push_back(object_value({
        {"value", number_value(parameter->max_value)},
        {"vertices", array_value(std::move(zero_vertices))},
    }));
    const std::string id = unique_id(*project->parameter_model, "shape");
    return object_value({
        {"id", string_value(id)},
        {"target_slot", string_value(data->slots()[slot_index].name)},
        {"target_attachment", string_value(attachment->name)},
        {"parameter", string_value(parameter->id)},
        {"blend_mode", string_value("additive_clamped")},
        {"keyforms", array_value(std::move(keyforms))},
    });
}

std::optional<Value> make_default_rotation_deformer(
    ShellState* state,
    std::string* error_out) {
    const auto* data = state->load_result.skeleton_data.get();
    const auto* project = state->session.project();
    if (data == nullptr || project == nullptr || !project->parameter_model.has_value()) {
        *error_out = "A continuous parameter and mesh slot are required.";
        return std::nullopt;
    }
    const auto parameter = std::find_if(
        project->parameter_model->parameters.begin(),
        project->parameter_model->parameters.end(),
        [](const ParameterAuthoringDefinition& candidate) {
            return candidate.type == ParameterAuthoringType::Continuous &&
                candidate.min_value < candidate.max_value;
        });
    std::size_t slot_index = 0U;
    for (; slot_index < data->slots().size(); ++slot_index) {
        const auto* attachment = data->find_attachment_source(
            slot_index, data->slots()[slot_index].setup_attachment);
        if (attachment != nullptr && attachment->mesh_geometry != nullptr) break;
    }
    if (parameter == project->parameter_model->parameters.end() ||
        slot_index >= data->slots().size()) {
        *error_out = "Rotation creation requires a non-degenerate continuous parameter and setup mesh.";
        return std::nullopt;
    }
    const std::string id = unique_id(*project->parameter_model, "deformer");
    Value::Array targets{string_value(data->slots()[slot_index].name)};
    Value::Array bindings{object_value({
        {"parameter", string_value(parameter->id)},
        {"axis", string_value("angle")},
    })};
    Value::Array pivot{number_value(0.0), number_value(0.0)};
    Value::Array keyforms{
        object_value({
            {"value", number_value(parameter->min_value)},
            {"angle", number_value(0.0)},
        }),
        object_value({
            {"value", number_value(parameter->max_value)},
            {"angle", number_value(0.0)},
        }),
    };
    return object_value({
        {"id", string_value(id)},
        {"name", string_value("Rotation Deformer")},
        {"kind", string_value("rotation")},
        {"target_slots", array_value(std::move(targets))},
        {"parameter_bindings", array_value(std::move(bindings))},
        {"pivot", array_value(std::move(pivot))},
        {"influence", number_value(1.0)},
        {"keyforms", array_value(std::move(keyforms))},
    });
}

std::optional<Value> make_default_warp_deformer(
    ShellState* state,
    std::string* error_out) {
    const auto* data = state->load_result.skeleton_data.get();
    const auto* project = state->session.project();
    if (data == nullptr || project == nullptr || !project->parameter_model.has_value()) {
        *error_out = "Two continuous parameters and a mesh are required.";
        return std::nullopt;
    }
    std::vector<const ParameterAuthoringDefinition*> parameters;
    for (const ParameterAuthoringDefinition& parameter :
         project->parameter_model->parameters) {
        if (parameter.type == ParameterAuthoringType::Continuous &&
            parameter.min_value < parameter.max_value) {
            parameters.push_back(&parameter);
        }
    }
    if (parameters.size() < 2U) {
        *error_out = "Warp creation requires two non-degenerate continuous parameters.";
        return std::nullopt;
    }
    std::size_t slot_index = 0U;
    const marrow::runtime::AttachmentData* attachment = nullptr;
    for (; slot_index < data->slots().size(); ++slot_index) {
        attachment = data->find_attachment_source(
            slot_index, data->slots()[slot_index].setup_attachment);
        if (attachment != nullptr && attachment->mesh_geometry != nullptr &&
            attachment->mesh_geometry->vertices.size() >= 4U) {
            break;
        }
    }
    if (slot_index >= data->slots().size() || attachment == nullptr) {
        *error_out = "Warp creation requires a setup-pose mesh attachment.";
        return std::nullopt;
    }
    double min_x = attachment->mesh_geometry->vertices[0];
    double max_x = min_x;
    double min_y = attachment->mesh_geometry->vertices[1];
    double max_y = min_y;
    for (std::size_t index = 0U;
         index + 1U < attachment->mesh_geometry->vertices.size();
         index += 2U) {
        min_x = std::min(min_x, attachment->mesh_geometry->vertices[index]);
        max_x = std::max(max_x, attachment->mesh_geometry->vertices[index]);
        min_y = std::min(min_y, attachment->mesh_geometry->vertices[index + 1U]);
        max_y = std::max(max_y, attachment->mesh_geometry->vertices[index + 1U]);
    }
    if (min_x == max_x) { min_x -= 1.0; max_x += 1.0; }
    if (min_y == max_y) { min_y -= 1.0; max_y += 1.0; }
    const auto lattice = [&]() {
        return Value::Array{
            number_value(min_x), number_value(min_y),
            number_value(max_x), number_value(min_y),
            number_value(min_x), number_value(max_y),
            number_value(max_x), number_value(max_y)};
    };
    Value::Array keyforms;
    for (const double x : {parameters[0]->min_value, parameters[0]->max_value}) {
        for (const double y : {parameters[1]->min_value, parameters[1]->max_value}) {
            keyforms.push_back(object_value({
                {"x", number_value(x)},
                {"y", number_value(y)},
                {"control_points", array_value(lattice())},
            }));
        }
    }
    Value::Array bindings{
        object_value({
            {"parameter", string_value(parameters[0]->id)},
            {"axis", string_value("x")},
        }),
        object_value({
            {"parameter", string_value(parameters[1]->id)},
            {"axis", string_value("y")},
        }),
    };
    Value::Array target_slots{string_value(data->slots()[slot_index].name)};
    const std::string id = unique_id(*project->parameter_model, "warp");
    return object_value({
        {"id", string_value(id)},
        {"name", string_value("Warp Deformer")},
        {"kind", string_value("warp")},
        {"target_slots", array_value(std::move(target_slots))},
        {"parameter_bindings", array_value(std::move(bindings))},
        {"grid_cols", number_value(2.0)},
        {"grid_rows", number_value(2.0)},
        {"control_points", array_value(lattice())},
        {"keyforms", array_value(std::move(keyforms))},
    });
}

std::optional<double> current_binding_value(
    const ShellState& state,
    const marrow::runtime::ParameterDeformerDefinition& deformer,
    marrow::runtime::ParameterDeformerAxis axis) {
    if (state.preview_skeleton == nullptr) return std::nullopt;
    const auto binding = std::find_if(
        deformer.parameter_bindings.begin(),
        deformer.parameter_bindings.end(),
        [&](const marrow::runtime::ParameterBindingDefinition& candidate) {
            return candidate.axis == axis;
        });
    if (binding == deformer.parameter_bindings.end() ||
        !binding->parameter_index.has_value() ||
        *binding->parameter_index >= state.preview_skeleton->parameter_values().size()) {
        return std::nullopt;
    }
    return state.preview_skeleton->parameter_values()[*binding->parameter_index];
}

std::optional<std::size_t> current_deformer_keyform_index(
    const ShellState& state,
    std::string_view deformer_id,
    const marrow::editor::ParameterDeformerAuthoringDefinition& authored_deformer) {
    if (state.load_result.skeleton_data == nullptr) return std::nullopt;
    const auto index =
        state.load_result.skeleton_data->find_parameter_deformer_index(deformer_id);
    if (!index.has_value()) return std::nullopt;
    const auto& deformer =
        state.load_result.skeleton_data->parameter_deformers()[*index];
    const auto axis_epsilon = [&](marrow::runtime::ParameterDeformerAxis axis) {
        if (state.session.project() == nullptr ||
            !state.session.project()->parameter_model.has_value()) {
            return 1e-9;
        }
        const auto binding = std::find_if(
            authored_deformer.parameter_bindings.begin(),
            authored_deformer.parameter_bindings.end(),
            [&](const marrow::runtime::ParameterBindingDefinition& candidate) {
                return candidate.axis == axis;
            });
        const ParameterAuthoringDefinition* parameter =
            binding == authored_deformer.parameter_bindings.end()
            ? nullptr
            : state.session.project()->parameter_model->find_parameter(binding->parameter);
        return 1e-9 * std::max(
            1.0,
            parameter == nullptr ? 1.0 : parameter->max_value - parameter->min_value);
    };
    if (deformer.kind == marrow::runtime::ParameterDeformerKind::Rotation) {
        const auto value = current_binding_value(
            state, deformer, marrow::runtime::ParameterDeformerAxis::Angle);
        if (!value.has_value()) return std::nullopt;
        for (std::size_t keyform_index = 0U;
             keyform_index < authored_deformer.rotation_keyforms.size();
             ++keyform_index) {
            if (std::abs(
                    authored_deformer.rotation_keyforms[keyform_index].value - *value) <=
                axis_epsilon(marrow::runtime::ParameterDeformerAxis::Angle)) {
                return keyform_index;
            }
        }
        return std::nullopt;
    }
    const auto x = current_binding_value(
        state, deformer, marrow::runtime::ParameterDeformerAxis::X);
    const auto y = current_binding_value(
        state, deformer, marrow::runtime::ParameterDeformerAxis::Y);
    if (!x.has_value() || !y.has_value()) return std::nullopt;
    for (std::size_t keyform_index = 0U;
         keyform_index < authored_deformer.warp_keyforms.size();
         ++keyform_index) {
        const auto& keyform = authored_deformer.warp_keyforms[keyform_index];
        if (std::abs(keyform.x - *x) <=
                axis_epsilon(marrow::runtime::ParameterDeformerAxis::X) &&
            std::abs(keyform.y - *y) <=
                axis_epsilon(marrow::runtime::ParameterDeformerAxis::Y)) {
            return keyform_index;
        }
    }
    return std::nullopt;
}

void resize_warp_grid(
    marrow::editor::ParameterDeformerAuthoringDefinition* deformer,
    std::size_t columns,
    std::size_t rows) {
    if (deformer == nullptr || columns < 2U || rows < 2U) return;
    double min_x = -1.0;
    double max_x = 1.0;
    double min_y = -1.0;
    double max_y = 1.0;
    if (!deformer->control_points.empty()) {
        min_x = max_x = deformer->control_points.front().x;
        min_y = max_y = deformer->control_points.front().y;
        for (const auto& point : deformer->control_points) {
            min_x = std::min(min_x, static_cast<double>(point.x));
            max_x = std::max(max_x, static_cast<double>(point.x));
            min_y = std::min(min_y, static_cast<double>(point.y));
            max_y = std::max(max_y, static_cast<double>(point.y));
        }
    }
    if (min_x == max_x) max_x = min_x + 1.0;
    if (min_y == max_y) max_y = min_y + 1.0;
    std::vector<marrow::runtime::AttachmentVertex> lattice;
    lattice.reserve(columns * rows);
    for (std::size_t row = 0U; row < rows; ++row) {
        const double y = min_y + ((max_y - min_y) * static_cast<double>(row) /
            static_cast<double>(rows - 1U));
        for (std::size_t column = 0U; column < columns; ++column) {
            const double x = min_x + ((max_x - min_x) * static_cast<double>(column) /
                static_cast<double>(columns - 1U));
            lattice.emplace_back(x, y);
        }
    }
    deformer->grid_cols = columns;
    deformer->grid_rows = rows;
    deformer->control_points = lattice;
    for (auto& keyform : deformer->warp_keyforms) {
        keyform.control_points = lattice;
    }
}

void draw_shapes_deformers_window(ShellState* state) {
    if (!ImGui::Begin(kParameterDeformersWindowTitle)) {
        ImGui::End();
        return;
    }
    const auto* project = state->session.project();
    const auto* model = project != nullptr && project->parameter_model.has_value()
        ? &*project->parameter_model
        : nullptr;
    const bool blocked = authoring_gesture_active(*state) ||
        state->session.transaction_active();
    ImGui::BeginDisabled(blocked);
    if (ImGui::Button("Add Shape")) {
        std::string error;
        auto definition = make_default_shape(state, &error);
        if (!definition.has_value()) {
            state->error_message = std::move(error);
        } else {
            const std::string id = *raw_id(*definition);
            if (apply_persistent_parameter_edit(
                    state,
                    "Create parameter shape " + id,
                    [definition = std::move(*definition)](
                        marrow::editor::ProjectData* candidate) mutable {
                        return marrow::editor::upsert_parameter_shape(
                            candidate, std::move(definition));
                    })) {
                g_selection.shape_id = id;
                ImGui::EndDisabled();
                ImGui::End();
                return;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Warp")) {
        std::string error;
        auto definition = make_default_warp_deformer(state, &error);
        if (!definition.has_value()) {
            state->error_message = std::move(error);
        } else {
            const std::string id = *raw_id(*definition);
            if (apply_persistent_parameter_edit(
                    state,
                    "Create warp deformer " + id,
                    [definition = std::move(*definition)](
                        marrow::editor::ProjectData* candidate) mutable {
                        return marrow::editor::upsert_parameter_deformer(
                            candidate, std::move(definition));
                    })) {
                g_selection.deformer_id = id;
                ImGui::EndDisabled();
                ImGui::End();
                return;
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Rotation")) {
        std::string error;
        auto definition = make_default_rotation_deformer(state, &error);
        if (!definition.has_value()) {
            state->error_message = std::move(error);
        } else {
            const std::string id = *raw_id(*definition);
            if (apply_persistent_parameter_edit(
                    state,
                    "Create parameter deformer " + id,
                    [definition = std::move(*definition)](
                        marrow::editor::ProjectData* candidate) mutable {
                        return marrow::editor::upsert_parameter_deformer(
                            candidate, std::move(definition));
                    })) {
                g_selection.deformer_id = id;
                ImGui::EndDisabled();
                ImGui::End();
                return;
            }
        }
    }
    ImGui::EndDisabled();
    if (model == nullptr) {
        ImGui::TextDisabled("No shapes or deformers.");
        ImGui::End();
        return;
    }
    ImGui::SeparatorText("Shapes");
    for (const marrow::editor::ParameterShapeAuthoringDefinition& shape :
         model->blend_shapes) {
        if (ImGui::Selectable(shape.id.c_str(), g_selection.shape_id == shape.id)) {
            g_selection.shape_id = shape.id;
            g_selection.deformer_id.clear();
        }
    }
    ImGui::SeparatorText("Deformers");
    for (const marrow::editor::ParameterDeformerAuthoringDefinition& deformer :
         model->deformers) {
        if (ImGui::Selectable(deformer.id.c_str(), g_selection.deformer_id == deformer.id)) {
            g_selection.deformer_id = deformer.id;
            g_selection.shape_id.clear();
        }
    }
    const std::string selected_id = !g_selection.shape_id.empty()
        ? g_selection.shape_id
        : g_selection.deformer_id;
    if (!selected_id.empty()) {
        const bool is_shape = !g_selection.shape_id.empty();
        const marrow::editor::ParameterShapeAuthoringDefinition* selected_shape =
            is_shape ? model->find_shape(selected_id) : nullptr;
        const marrow::editor::ParameterDeformerAuthoringDefinition* selected_deformer =
            is_shape ? nullptr : model->find_deformer(selected_id);
        if (selected_shape != nullptr || selected_deformer != nullptr) {
            ImGui::SeparatorText("Definition");
            if (selected_shape != nullptr) {
                bool normalized = selected_shape->blend_mode ==
                    marrow::runtime::ParameterShapeBlendMode::NormalizedOverride;
                if (ImGui::Checkbox("Normalized Override", &normalized)) {
                    // Deep-copy the definition only once an edit happened.
                    auto edited = *selected_shape;
                    edited.blend_mode = normalized
                        ? marrow::runtime::ParameterShapeBlendMode::NormalizedOverride
                        : marrow::runtime::ParameterShapeBlendMode::AdditiveClamped;
                    (void)apply_parameter_shape_definition_edit(
                        state,
                        "Edit shape blend mode " + edited.id,
                        std::move(edited));
                    ImGui::End();
                    return;
                }
            } else if (selected_deformer != nullptr) {
                // Widgets read from the const selection; the definition (warp
                // grids, keyforms, preserved source) is deep-copied only once
                // an edit actually happened.
                std::string edited_name = selected_deformer->name;
                std::optional<std::string> edited_parent = selected_deformer->parent;
                double edited_influence = selected_deformer->influence;
                int columns = static_cast<int>(selected_deformer->grid_cols);
                int rows = static_cast<int>(selected_deformer->grid_rows);
                bool grid_changed = false;
                bool definition_changed = input_string_field("Name", &edited_name);
                if (ImGui::BeginCombo(
                        "Parent",
                        edited_parent.has_value() ? edited_parent->c_str() : "<none>")) {
                    if (ImGui::Selectable("<none>", !edited_parent.has_value())) {
                        edited_parent.reset();
                        definition_changed = true;
                    }
                    for (const auto& candidate : model->deformers) {
                        if (candidate.id == selected_deformer->id) continue;
                        const bool is_selected = edited_parent ==
                            std::optional<std::string>(candidate.id);
                        if (ImGui::Selectable(candidate.id.c_str(), is_selected)) {
                            edited_parent = candidate.id;
                            definition_changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (selected_deformer->kind ==
                    marrow::runtime::ParameterDeformerKind::Rotation) {
                    definition_changed |= ImGui::InputDouble(
                        "Influence", &edited_influence, 0.01, 0.1);
                } else {
                    grid_changed = ImGui::InputInt("Grid Columns", &columns);
                    grid_changed |= ImGui::InputInt("Grid Rows", &rows);
                    if (grid_changed && columns >= 2 && rows >= 2) {
                        definition_changed = true;
                    } else {
                        grid_changed = false;
                    }
                }
                if (definition_changed) {
                    auto edited = *selected_deformer;
                    edited.name = std::move(edited_name);
                    edited.parent = std::move(edited_parent);
                    edited.influence = edited_influence;
                    if (grid_changed) {
                        resize_warp_grid(
                            &edited,
                            static_cast<std::size_t>(columns),
                            static_cast<std::size_t>(rows));
                    }
                    (void)apply_parameter_deformer_definition_edit(
                        state,
                        "Edit parameter deformer " + edited.id,
                        std::move(edited));
                    ImGui::End();
                    return;
                }
            }
            ImGui::SeparatorText("Target Binding");
            if (is_shape) {
                const char* preview = selected_shape->parameter.empty()
                    ? "<unbound>"
                    : selected_shape->parameter.c_str();
                if (ImGui::BeginCombo("Parameter", preview)) {
                    for (const ParameterAuthoringDefinition& candidate : model->parameters) {
                        if (candidate.type != ParameterAuthoringType::Continuous) continue;
                        const bool selected = selected_shape->parameter == candidate.id;
                        if (ImGui::Selectable(candidate.id.c_str(), selected)) {
                            auto edited = *selected_shape;
                            edited.parameter = candidate.id;
                            edited.parameter_index.reset();
                            (void)apply_parameter_shape_definition_edit(
                                state,
                                "Bind shape parameter " + edited.id,
                                std::move(edited));
                            ImGui::EndCombo();
                            ImGui::End();
                            return;
                        }
                    }
                    ImGui::EndCombo();
                }
            } else {
                for (std::size_t binding_index = 0U;
                     binding_index < selected_deformer->parameter_bindings.size();
                     ++binding_index) {
                    const auto& binding =
                        selected_deformer->parameter_bindings[binding_index];
                    const std::string label = "Parameter (" +
                        std::string(parameter_deformer_axis_name(binding.axis)) + ")";
                    const char* preview = binding.parameter.empty()
                        ? "<unbound>"
                        : binding.parameter.c_str();
                    if (ImGui::BeginCombo(
                            (label + "##" + std::to_string(binding_index)).c_str(),
                            preview)) {
                        for (const ParameterAuthoringDefinition& candidate : model->parameters) {
                            if (candidate.type != ParameterAuthoringType::Continuous) continue;
                            const bool selected = binding.parameter == candidate.id;
                            if (ImGui::Selectable(candidate.id.c_str(), selected)) {
                                auto edited = *selected_deformer;
                                edited.parameter_bindings[binding_index].parameter = candidate.id;
                                edited.parameter_bindings[binding_index].parameter_index.reset();
                                (void)apply_parameter_deformer_definition_edit(
                                    state,
                                    "Bind deformer parameter " + edited.id,
                                    std::move(edited));
                                ImGui::EndCombo();
                                ImGui::End();
                                return;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }

            const auto* data = state->load_result.skeleton_data.get();
            if (data != nullptr && !data->slots().empty()) {
                const std::string current_target = is_shape
                    ? selected_shape->target_slot
                    : selected_deformer->target_slots.empty()
                        ? std::string{}
                        : selected_deformer->target_slots.front();
                if (ImGui::BeginCombo(
                        "Target Slot",
                        current_target.empty() ? "<none>" : current_target.c_str())) {
                    for (std::size_t slot_index = 0U; slot_index < data->slots().size(); ++slot_index) {
                        const auto& slot = data->slots()[slot_index];
                        const auto* target_attachment = data->find_attachment_source(
                            slot_index, slot.setup_attachment);
                        if (target_attachment == nullptr ||
                            target_attachment->mesh_geometry == nullptr) {
                            continue;
                        }
                        if (ImGui::Selectable(slot.name.c_str(), current_target == slot.name)) {
                            if (is_shape) {
                                auto edited = *selected_shape;
                                edited.target_slot = slot.name;
                                edited.target_attachment = target_attachment->name;
                                (void)apply_parameter_shape_definition_edit(
                                    state,
                                    "Bind parameter target " + edited.id,
                                    std::move(edited));
                            } else {
                                auto edited = *selected_deformer;
                                edited.target_slots = {slot.name};
                                edited.target_slot_indices.clear();
                                (void)apply_parameter_deformer_definition_edit(
                                    state,
                                    "Bind parameter target " + edited.id,
                                    std::move(edited));
                            }
                            ImGui::EndCombo();
                            ImGui::End();
                            return;
                        }
                    }
                    ImGui::EndCombo();
                }
            }
            if (!is_shape) {
                ImGui::SeparatorText("Geometry Gesture");
                const std::optional<std::size_t> current_keyform =
                    current_deformer_keyform_index(
                          *state, selected_id, *selected_deformer);
                if (!current_keyform.has_value()) {
                    ImGui::TextDisabled(
                        "No keyform at the current coordinates.");
                    if (ImGui::Button("Capture + Edit Geometry")) {
                        const bool rotation = selected_deformer != nullptr &&
                            selected_deformer->kind ==
                                marrow::runtime::ParameterDeformerKind::Rotation;
                        const double initial_value = rotation
                            ? selected_deformer->pivot.x
                            : selected_deformer != nullptr &&
                                    !selected_deformer->control_points.empty()
                                ? selected_deformer->control_points.front().x
                                : 0.0;
                        g_pending_missing_geometry_capture = PendingMissingGeometryCapture{
                            selected_id,
                            rotation ? "pivot:x" : "warp:first-x",
                            initial_value};
                        ImGui::OpenPopup("Capture Missing Geometry Keyform?");
                    }
                } else if (selected_deformer->kind ==
                           marrow::runtime::ParameterDeformerKind::Rotation) {
                    if (draw_parameter_geometry_scalar(
                            state,
                            selected_id,
                            "pivot:x",
                            "Pivot X",
                            selected_deformer->pivot.x) ||
                        draw_parameter_geometry_scalar(
                            state,
                            selected_id,
                            "pivot:y",
                            "Pivot Y",
                            selected_deformer->pivot.y)) {
                        ImGui::End();
                        return;
                    }
                } else if (selected_deformer->kind ==
                               marrow::runtime::ParameterDeformerKind::Warp &&
                           *current_keyform < selected_deformer->warp_keyforms.size()) {
                    const auto& control_points =
                        selected_deformer->warp_keyforms[*current_keyform].control_points;
                    for (std::size_t point_index = 0U;
                         point_index < control_points.size();
                         ++point_index) {
                        const std::string component_prefix =
                            "keyform:" + std::to_string(*current_keyform) + ":";
                        const std::string x_field = component_prefix +
                            std::to_string(point_index * 2U);
                        const std::string y_field = component_prefix +
                            std::to_string((point_index * 2U) + 1U);
                        const std::string x_label =
                            "Lattice Point " + std::to_string(point_index) + " X";
                        const std::string y_label =
                            "Lattice Point " + std::to_string(point_index) + " Y";
                        if (draw_parameter_geometry_scalar(
                                state,
                                selected_id,
                                x_field,
                                x_label.c_str(),
                                control_points[point_index].x) ||
                            draw_parameter_geometry_scalar(
                                state,
                                selected_id,
                                y_field,
                                y_label.c_str(),
                                control_points[point_index].y)) {
                            ImGui::End();
                            return;
                        }
                    }
                }
            }
        }
        ImGui::BeginDisabled(blocked);
        if (ImGui::Button("Capture Current Keyform")) {
            const std::string id = selected_id;
            if (capture_current_keyform(state, id, false)) {
                ImGui::EndDisabled();
                ImGui::End();
                return;
            } else if (state->error_message.find("already exists") != std::string::npos) {
                g_pending_capture_id = id;
                ImGui::OpenPopup("Replace Parameter Keyform?");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            const bool is_shape = !g_selection.shape_id.empty();
            const std::string id = selected_id;
            const bool deleted = apply_persistent_parameter_edit(
                state,
                "Delete parameter deformer " + id,
                [is_shape, id](marrow::editor::ProjectData* candidate) {
                    return is_shape
                        ? marrow::editor::delete_parameter_shape(candidate, id)
                        : marrow::editor::delete_parameter_deformer(candidate, id);
                });
            if (deleted) {
                g_selection.shape_id.clear();
                g_selection.deformer_id.clear();
            }
        }
        ImGui::EndDisabled();
    }
    draw_missing_geometry_capture_popup(state);
    draw_capture_replace_popup(state);
    ImGui::End();
}

Value make_default_expression(const marrow::editor::ParameterModel& model) {
    const std::string id = unique_id(model, "expression");
    const ParameterAuthoringDefinition& parameter = model.parameters.front();
    Value::Array targets{object_value({
        {"parameter", string_value(parameter.id)},
        {"value", number_value(parameter.default_value)},
    })};
    return object_value({
        {"id", string_value(id)},
        {"name", string_value("Expression")},
        {"targets", array_value(std::move(targets))},
        {"duration", number_value(0.0)},
        {"blend", string_value("override")},
        {"priority", number_value(0.0)},
        {"reset_policy", string_value("restore")},
    });
}

void draw_expressions_window(ShellState* state) {
    if (!ImGui::Begin(kExpressionsWindowTitle)) {
        ImGui::End();
        return;
    }
    const auto* project = state->session.project();
    const auto* model = project != nullptr && project->parameter_model.has_value()
        ? &*project->parameter_model
        : nullptr;
    const bool blocked = authoring_gesture_active(*state) ||
        state->session.transaction_active();
    ImGui::BeginDisabled(blocked || model == nullptr || model->parameters.empty());
    if (ImGui::Button("Add Expression")) {
        Value definition = make_default_expression(*model);
        const std::string id = *raw_id(definition);
        if (apply_persistent_parameter_edit(
                state,
                "Create expression " + id,
                [definition = std::move(definition)](
                    marrow::editor::ProjectData* candidate) mutable {
                    return marrow::editor::upsert_expression(
                        candidate, std::move(definition));
                })) {
            g_selection.expression_id = id;
            ImGui::EndDisabled();
            ImGui::End();
            return;
        }
    }
    ImGui::EndDisabled();
    model = state->session.project() != nullptr &&
            state->session.project()->parameter_model.has_value()
        ? &*state->session.project()->parameter_model
        : nullptr;
    if (model != nullptr) {
        for (const marrow::editor::ExpressionAuthoringDefinition& expression :
             model->expressions) {
            if (ImGui::Selectable(
                    expression.id.c_str(), g_selection.expression_id == expression.id)) {
                g_selection.expression_id = expression.id;
            }
        }
    }
    const marrow::editor::ExpressionAuthoringDefinition* selected_expression =
        model == nullptr ? nullptr : model->find_expression(g_selection.expression_id);
    if (selected_expression != nullptr) {
        const auto commit_expression_change = [&](auto mutate) {
            auto edited = *selected_expression;
            mutate(&edited);
            return apply_expression_definition_edit(
                state, "Edit expression " + edited.id, std::move(edited));
        };

        std::string name = selected_expression->name;
        if (input_string_field("Name", &name)) {
            (void)commit_expression_change(
                [&](auto* edited) { edited->name = std::move(name); });
            ImGui::End();
            return;
        }
        double duration = selected_expression->duration;
        if (ImGui::InputDouble("Duration", &duration, 0.01, 0.1)) {
            (void)commit_expression_change(
                [&](auto* edited) { edited->duration = duration; });
            ImGui::End();
            return;
        }
        int priority = selected_expression->priority;
        if (ImGui::InputInt("Priority", &priority)) {
            (void)commit_expression_change(
                [&](auto* edited) { edited->priority = priority; });
            ImGui::End();
            return;
        }
        bool override_blend = selected_expression->blend ==
            marrow::runtime::ExpressionBlend::Override;
        if (ImGui::Checkbox("Override Blend", &override_blend)) {
            (void)commit_expression_change([&](auto* edited) {
                edited->blend = override_blend
                    ? marrow::runtime::ExpressionBlend::Override
                    : marrow::runtime::ExpressionBlend::Additive;
            });
            ImGui::End();
            return;
        }
        ImGui::SameLine();
        bool hold = selected_expression->reset_policy ==
            marrow::runtime::ExpressionResetPolicy::Hold;
        if (ImGui::Checkbox("Hold on Deactivate", &hold)) {
            (void)commit_expression_change([&](auto* edited) {
                edited->reset_policy = hold
                    ? marrow::runtime::ExpressionResetPolicy::Hold
                    : marrow::runtime::ExpressionResetPolicy::Restore;
            });
            ImGui::End();
            return;
        }
        ImGui::SeparatorText("Targets");
        for (std::size_t target_index = 0U;
             target_index < selected_expression->targets.size();
             ++target_index) {
            const auto& target = selected_expression->targets[target_index];
            ImGui::PushID(static_cast<int>(target_index));
            if (ImGui::BeginCombo("Parameter", target.parameter.c_str())) {
                for (const ParameterAuthoringDefinition& parameter : model->parameters) {
                    const bool used_elsewhere = std::any_of(
                        selected_expression->targets.begin(),
                        selected_expression->targets.end(),
                        [&](const marrow::runtime::ExpressionTargetDefinition& candidate) {
                            return &candidate != &target &&
                                candidate.parameter == parameter.id;
                        });
                    if (used_elsewhere) continue;
                    const bool is_selected = target.parameter == parameter.id;
                    if (ImGui::Selectable(parameter.id.c_str(), is_selected)) {
                        (void)commit_expression_change([&](auto* edited) {
                            edited->targets[target_index].parameter = parameter.id;
                            edited->targets[target_index].parameter_index.reset();
                        });
                        ImGui::EndCombo();
                        ImGui::PopID();
                        ImGui::End();
                        return;
                    }
                }
                ImGui::EndCombo();
            }
            double target_value = target.value;
            if (ImGui::InputDouble("Value", &target_value, 0.01, 0.1)) {
                (void)commit_expression_change([&](auto* edited) {
                    edited->targets[target_index].value = target_value;
                });
                ImGui::PopID();
                ImGui::End();
                return;
            }
            if (selected_expression->targets.size() > 1U &&
                ImGui::SmallButton("Remove Target")) {
                (void)commit_expression_change([&](auto* edited) {
                    edited->targets.erase(
                        edited->targets.begin() +
                        static_cast<std::ptrdiff_t>(target_index));
                });
                ImGui::PopID();
                ImGui::End();
                return;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add Target")) {
            const auto unused = std::find_if(
                model->parameters.begin(), model->parameters.end(),
                [&](const ParameterAuthoringDefinition& parameter) {
                    return std::none_of(
                        selected_expression->targets.begin(),
                        selected_expression->targets.end(),
                        [&](const marrow::runtime::ExpressionTargetDefinition& target) {
                            return target.parameter == parameter.id;
                        });
                });
            if (unused != model->parameters.end()) {
                (void)commit_expression_change([&](auto* edited) {
                    edited->targets.push_back(
                        {unused->id, std::nullopt, unused->default_value});
                });
                ImGui::End();
                return;
            }
        }
        const bool active = state->session.preview_state().active_expression ==
            std::optional<std::string>(g_selection.expression_id);
        if (ImGui::Button(active ? "Deactivate Preview" : "Activate Preview")) {
            const auto result = state->session.set_preview_expression(
                active ? std::nullopt
                       : std::optional<std::string>(g_selection.expression_id));
            sync_shell_from_editor_session(state);
            if (!result) state->error_message = result.error->format();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(blocked);
        if (ImGui::Button("Delete Expression")) {
            const std::string id = g_selection.expression_id;
            if (apply_persistent_parameter_edit(
                    state,
                    "Delete expression " + id,
                    [id](marrow::editor::ProjectData* candidate) {
                        return marrow::editor::delete_expression(candidate, id);
                    })) {
                g_selection.expression_id.clear();
            }
        }
        ImGui::EndDisabled();
    }
    ImGui::End();
}

Value make_default_lip_mapping(const ParameterAuthoringDefinition& parameter) {
    return object_value({
        {"source", string_value("amplitude")},
        {"parameter", string_value(parameter.id)},
        {"scale", number_value(1.0)},
        {"bias", number_value(0.0)},
        {"attack", number_value(0.0)},
        {"release", number_value(0.0)},
        {"smoothing", number_value(0.0)},
    });
}

Value build_lip_mapping_value(
    const marrow::editor::LipSyncMappingAuthoringDefinition& mapping) {
    marrow::editor::LipSyncAuthoringDefinition section;
    section.mappings.push_back(mapping);
    Value value = marrow::editor::build_lip_sync_authoring_value(section);
    const Value* mappings = marrow::runtime::json::find_member(value, "mappings");
    return mappings != nullptr && mappings->is_array() && !mappings->as_array().empty()
        ? mappings->as_array().front()
        : object_value();
}

bool apply_lip_sync_mapping_definition_edit(
    ShellState* state,
    std::string label,
    std::string previous_parameter,
    marrow::editor::LipSyncMappingAuthoringDefinition definition) {
    const std::string next_parameter = definition.parameter;
    Value value = build_lip_mapping_value(definition);
    return apply_persistent_parameter_edit(
        state,
        std::move(label),
        [previous_parameter = std::move(previous_parameter),
         next_parameter,
         value = std::move(value)](
            marrow::editor::ProjectData* project) mutable {
            if (previous_parameter != next_parameter) {
                AuthoringResult removed = marrow::editor::delete_lip_sync_mapping(
                    project, previous_parameter);
                if (!removed) return removed;
            }
            return marrow::editor::upsert_lip_sync_mapping(
                project, std::move(value));
        });
}

void draw_lip_sync_window(ShellState* state) {
    if (!ImGui::Begin(kLipSyncWindowTitle)) {
        ImGui::End();
        return;
    }
    const auto* project = state->session.project();
    const auto* model = project != nullptr && project->parameter_model.has_value()
        ? &*project->parameter_model
        : nullptr;
    const bool blocked = authoring_gesture_active(*state) ||
        state->session.transaction_active();
    ImGui::BeginDisabled(blocked || model == nullptr || model->parameters.empty());
    if (ImGui::Button("Map First Parameter") && model != nullptr) {
        const std::string id = model->parameters.front().id;
        Value mapping = make_default_lip_mapping(model->parameters.front());
        if (apply_persistent_parameter_edit(
                state,
                "Map lip sync to " + id,
                [mapping = std::move(mapping)](
                    marrow::editor::ProjectData* candidate) mutable {
                    return marrow::editor::upsert_lip_sync_mapping(
                        candidate, std::move(mapping));
                })) {
            g_selection.lip_parameter_id = id;
            ImGui::EndDisabled();
            ImGui::End();
            return;
        }
    }
    ImGui::EndDisabled();
    model = state->session.project() != nullptr &&
            state->session.project()->parameter_model.has_value()
        ? &*state->session.project()->parameter_model
        : nullptr;
    const marrow::editor::LipSyncMappingAuthoringDefinition* selected_mapping = nullptr;
    if (model != nullptr) {
        for (const marrow::editor::LipSyncMappingAuthoringDefinition& mapping :
             model->lip_sync.mappings) {
            if (ImGui::Selectable(
                    mapping.parameter.c_str(),
                    g_selection.lip_parameter_id == mapping.parameter)) {
                g_selection.lip_parameter_id = mapping.parameter;
            }
            if (g_selection.lip_parameter_id == mapping.parameter) selected_mapping = &mapping;
        }
    }
    if (selected_mapping != nullptr) {
        const auto commit_mapping_change = [&](auto mutate) {
            auto edited = *selected_mapping;
            const std::string previous_parameter = edited.parameter;
            mutate(&edited);
            const std::string next_parameter = edited.parameter;
            const bool committed = apply_lip_sync_mapping_definition_edit(
                state,
                "Edit lip-sync mapping " + previous_parameter,
                previous_parameter,
                std::move(edited));
            g_selection.lip_parameter_id = next_parameter;
            return committed;
        };

        bool phoneme_source = selected_mapping->source ==
            marrow::runtime::LipSyncSource::Phoneme;
        if (ImGui::Checkbox("Phoneme Source", &phoneme_source)) {
            (void)commit_mapping_change([&](auto* edited) {
                edited->source = phoneme_source
                    ? marrow::runtime::LipSyncSource::Phoneme
                    : marrow::runtime::LipSyncSource::Amplitude;
            });
            ImGui::End();
            return;
        }
        ImGui::BeginDisabled(blocked);
        if (ImGui::BeginCombo("Target Parameter", selected_mapping->parameter.c_str())) {
            for (const ParameterAuthoringDefinition& parameter : model->parameters) {
                const bool is_selected = selected_mapping->parameter == parameter.id;
                if (ImGui::Selectable(parameter.id.c_str(), is_selected)) {
                    (void)commit_mapping_change([&](auto* edited) {
                        edited->parameter = parameter.id;
                        edited->parameter_index.reset();
                    });
                    ImGui::EndCombo();
                    ImGui::EndDisabled();
                    ImGui::End();
                    return;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::EndDisabled();
        const auto draw_mapping_scalar = [&](const char* label,
                                             double value,
                                             double step,
                                             double step_fast,
                                             auto assign) {
            if (!ImGui::InputDouble(label, &value, step, step_fast)) {
                return false;
            }
            (void)commit_mapping_change(
                [&](auto* edited) { assign(edited, value); });
            return true;
        };
        if (draw_mapping_scalar(
                "Scale",
                selected_mapping->scale,
                0.05,
                0.25,
                [](auto* edited, double value) { edited->scale = value; }) ||
            draw_mapping_scalar(
                "Bias",
                selected_mapping->bias,
                0.05,
                0.25,
                [](auto* edited, double value) { edited->bias = value; }) ||
            draw_mapping_scalar(
                "Attack",
                selected_mapping->attack,
                0.01,
                0.1,
                [](auto* edited, double value) { edited->attack = value; }) ||
            draw_mapping_scalar(
                "Release",
                selected_mapping->release,
                0.01,
                0.1,
                [](auto* edited, double value) { edited->release = value; }) ||
            draw_mapping_scalar(
                "Smoothing",
                selected_mapping->smoothing,
                0.01,
                0.1,
                [](auto* edited, double value) { edited->smoothing = value; })) {
            ImGui::End();
            return;
        }
        ImGui::SeparatorText("Phoneme Map");
        for (std::size_t phoneme_index = 0U;
             phoneme_index < selected_mapping->phoneme_map.size();
             ++phoneme_index) {
            const auto& phoneme = selected_mapping->phoneme_map[phoneme_index];
            ImGui::PushID(static_cast<int>(phoneme_index));
            std::string phoneme_name = phoneme.phoneme;
            if (input_string_field("Phoneme", &phoneme_name)) {
                (void)commit_mapping_change([&](auto* edited) {
                    edited->phoneme_map[phoneme_index].phoneme =
                        std::move(phoneme_name);
                });
                ImGui::PopID();
                ImGui::End();
                return;
            }
            double phoneme_value = phoneme.value;
            if (ImGui::InputDouble("Value", &phoneme_value, 0.05, 0.25)) {
                (void)commit_mapping_change([&](auto* edited) {
                    edited->phoneme_map[phoneme_index].value = phoneme_value;
                });
                ImGui::PopID();
                ImGui::End();
                return;
            }
            if (ImGui::SmallButton("Remove Phoneme")) {
                (void)commit_mapping_change([&](auto* edited) {
                    edited->phoneme_map.erase(
                        edited->phoneme_map.begin() +
                        static_cast<std::ptrdiff_t>(phoneme_index));
                });
                ImGui::PopID();
                ImGui::End();
                return;
            }
            ImGui::PopID();
        }
        if (ImGui::Button("Add Phoneme")) {
            std::string phoneme = "A";
            for (std::size_t suffix = 1U;
                 std::any_of(
                     selected_mapping->phoneme_map.begin(),
                     selected_mapping->phoneme_map.end(),
                     [&](const marrow::runtime::PhonemeValueDefinition& candidate) {
                         return candidate.phoneme == phoneme;
                     });
                 ++suffix) {
                phoneme = "A" + std::to_string(suffix);
            }
            (void)commit_mapping_change([&](auto* edited) {
                edited->phoneme_map.push_back({std::move(phoneme), 0.0});
            });
            ImGui::End();
            return;
        }
        ImGui::BeginDisabled(blocked);
        if (ImGui::Button("Delete Mapping")) {
            const std::string id = g_selection.lip_parameter_id;
            if (apply_persistent_parameter_edit(
                    state,
                    "Delete lip-sync mapping " + id,
                    [id](marrow::editor::ProjectData* candidate) {
                        return marrow::editor::delete_lip_sync_mapping(candidate, id);
                    })) {
                g_selection.lip_parameter_id.clear();
            }
        }
        ImGui::EndDisabled();
    }

    double amplitude = state->session.preview_state().synthetic_amplitude;
    const double amplitude_min = 0.0;
    const double amplitude_max = 1.0;
    if (ImGui::SliderScalar(
            "Synthetic Amplitude",
            ImGuiDataType_Double,
            &amplitude,
            &amplitude_min,
            &amplitude_max,
            "%.3f")) {
        const auto result = state->session.set_preview_lip_input(
            amplitude, state->session.preview_state().synthetic_phoneme);
        sync_shell_from_editor_session(state);
        if (!result) state->error_message = result.error->format();
    }
    std::array<char, 128> phoneme{};
    const std::string& current_phoneme =
        state->session.preview_state().synthetic_phoneme;
    const std::size_t phoneme_length =
        std::min(current_phoneme.size(), phoneme.size() - 1U);
    std::copy_n(current_phoneme.data(), phoneme_length, phoneme.data());
    if (ImGui::InputText("Synthetic Phoneme", phoneme.data(), phoneme.size())) {
        const auto result = state->session.set_preview_lip_input(
            state->session.preview_state().synthetic_amplitude,
            std::string(phoneme.data()));
        sync_shell_from_editor_session(state);
        if (!result) state->error_message = result.error->format();
    }
    ImGui::End();
}

} // namespace

void draw_parameter_windows(ShellState* state) {
    draw_parameters_window(state);
    draw_shapes_deformers_window(state);
    draw_expressions_window(state);
    draw_lip_sync_window(state);
}

bool validate_parameter_mode_shell_smoke(
    ShellState* state,
    const Options& options,
    ImGuiIO& io) {
    if (state == nullptr || state->session.project() == nullptr ||
        !state->session.project()->parameter_model.has_value() ||
        state->load_result.skeleton_data == nullptr || state->preview_skeleton == nullptr) {
        std::cerr << "Parameter shell smoke requires a parameter project preview.\n";
        return false;
    }

    const std::string animation_before = state->selected_animation_name;
    const double time_before = state->timeline_time_seconds;
    const bool queue_before = state->preview_queue_enabled;
    state->timeline_playing = true;
    state->session.set_playing(true);
    apply_shell_mode(state, ShellMode::Parameter);
    if (current_shell_mode(state) != ShellMode::Parameter ||
        state->selected_animation_name != animation_before ||
        state->timeline_time_seconds != time_before ||
        state->preview_queue_enabled != queue_before || state->timeline_playing ||
        state->session.preview_state().playing || state->weight_paint.enabled) {
        std::cerr << "Parameter mode did not preserve the current pose while stopping playback.\n";
        return false;
    }

    const bool dirty_before_unclamped_preview = state->session.dirty();
    const std::string project_before_unclamped_preview =
        marrow::editor::serialize_project(*state->session.project());
    if (!state->session.set_preview_parameter_value("face.variant", 7.4)) {
        std::cerr << "Could not preview an out-of-range unclamped parameter.\n";
        return false;
    }
    const auto variant_index =
        state->load_result.skeleton_data->find_parameter_index("face.variant");
    const auto variant_direct =
        state->session.preview_state().direct_parameter_values.find("face.variant");
    if (!variant_index.has_value() ||
        variant_direct == state->session.preview_state().direct_parameter_values.end() ||
        std::abs(variant_direct->second - 7.4) > 1e-9 ||
        std::abs(state->preview_skeleton->parameter_values()[*variant_index] - 7.0) > 1e-9 ||
        state->session.dirty() != dirty_before_unclamped_preview ||
        marrow::editor::serialize_project(*state->session.project()) !=
            project_before_unclamped_preview) {
        std::cerr << "Unclamped discrete preview input was not preserved without persisting.\n";
        return false;
    }

    marrow::editor::ProjectData collision_candidate = *state->session.project();
    const AuthoringResult collision = marrow::editor::capture_current_deformer_keyform(
        &collision_candidate,
        *state->load_result.skeleton_data,
        *state->preview_skeleton,
        "mouth.open.shape",
        false);
    if (collision || collision.error.find("already exists") == std::string::npos) {
        std::cerr << "Capture did not reject a keyform collision without replace confirmation.\n";
        return false;
    }
    if (!apply_persistent_parameter_edit(
            state,
            "Replace captured shape keyform",
            [state](marrow::editor::ProjectData* project) {
                return marrow::editor::capture_current_deformer_keyform(
                    project,
                    *state->load_result.skeleton_data,
                    *state->preview_skeleton,
                    "mouth.open.shape",
                    true);
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }

    ParameterAuthoringDefinition secondary;
    secondary.id = "editor.smoke.axis";
    secondary.name = "Smoke Axis";
    secondary.min_value = -1.0;
    secondary.max_value = 1.0;
    secondary.default_value = 0.25;
    secondary.ui_step = 0.05;
    secondary.units = "ratio";
    if (!apply_persistent_parameter_edit(
            state,
            "Create smoke parameter and group",
            [secondary](marrow::editor::ProjectData* project) mutable {
                AuthoringResult result = marrow::editor::create_parameter(
                    project, std::move(secondary));
                if (!result) return result;
                ParameterGroupAuthoringDefinition group;
                group.id = "editor.smoke.group";
                group.name = "Smoke Group";
                group.parameter_ids = {"editor.smoke.axis"};
                group.collapsed = true;
                group.color_tag = "smoke-orange";
                group.exclusive_mode = "single";
                return marrow::editor::create_parameter_group(project, std::move(group));
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    const auto secondary_preview =
        state->session.preview_state().direct_parameter_values.find("editor.smoke.axis");
    const auto* authored_secondary =
        state->session.project()->parameter_model->find_parameter("editor.smoke.axis");
    const auto* authored_group =
        state->session.project()->parameter_model->find_group("editor.smoke.group");
    const auto preserved_variant_direct =
        state->session.preview_state().direct_parameter_values.find("face.variant");
    if (secondary_preview == state->session.preview_state().direct_parameter_values.end() ||
        std::abs(secondary_preview->second - 0.25) > 1e-9 ||
        authored_secondary == nullptr || authored_secondary->name != "Smoke Axis" ||
        authored_secondary->ui_step != std::optional<double>(0.05) ||
        authored_secondary->units != std::optional<std::string>("ratio") ||
        authored_group == nullptr || authored_group->name != "Smoke Group" ||
        !authored_group->collapsed ||
        authored_group->color_tag != std::optional<std::string>("smoke-orange") ||
        authored_group->exclusive_mode != std::optional<std::string>("single") ||
        preserved_variant_direct ==
            state->session.preview_state().direct_parameter_values.end() ||
        preserved_variant_direct->second != 7.4 ||
        std::abs(state->preview_skeleton->parameter_values()[*variant_index] - 7.0) > 1e-9) {
        std::cerr << "Parameter/group fields or preview preservation failed after rebuild.\n";
        return false;
    }
    marrow::editor::ProjectData dependency_candidate = *state->session.project();
    const AuthoringResult blocked_delete = marrow::editor::delete_parameter(
        &dependency_candidate, "editor.smoke.axis");
    if (blocked_delete || blocked_delete.dependencies.empty()) {
        std::cerr << "Referenced parameter deletion did not report dependencies.\n";
        return false;
    }
    if (!state->session.set_preview_parameter_value("editor.smoke.axis", -1.0)) {
        std::cerr << "Could not position the warp preview on an existing Cartesian keyform.\n";
        return false;
    }
    sync_shell_from_editor_session(state);

    std::string definition_error;
    std::optional<Value> warp = make_default_warp_deformer(state, &definition_error);
    if (!warp.has_value()) {
        std::cerr << definition_error << '\n';
        return false;
    }
    warp->as_object()["panel_marker"] = string_value("warp-top");
    Value* warp_bindings =
        marrow::runtime::json::find_member(*warp, "parameter_bindings");
    Value* warp_keyforms = marrow::runtime::json::find_member(*warp, "keyforms");
    if (warp_bindings == nullptr || !warp_bindings->is_array() ||
        warp_bindings->as_array().empty() || warp_keyforms == nullptr ||
        !warp_keyforms->is_array() || warp_keyforms->as_array().empty()) {
        std::cerr << "Default warp did not expose marker test entries.\n";
        return false;
    }
    warp_bindings->as_array().front().as_object()["panel_marker"] =
        string_value("warp-binding");
    warp_keyforms->as_array().front().as_object()["panel_marker"] =
        string_value("warp-keyform");
    const std::string warp_id = *raw_id(*warp);
    if (!apply_persistent_parameter_edit(
            state,
            "Create warp deformer smoke",
            [definition = std::move(*warp)](
                marrow::editor::ProjectData* project) mutable {
                return marrow::editor::upsert_parameter_deformer(
                    project, std::move(definition));
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    const marrow::editor::ParameterDeformerAuthoringDefinition* live_warp =
        state->session.project()->parameter_model->find_deformer(warp_id);
    if (live_warp == nullptr || live_warp->name != "Warp Deformer" ||
        live_warp->kind != marrow::runtime::ParameterDeformerKind::Warp ||
        live_warp->grid_cols != 2U || live_warp->grid_rows != 2U ||
        live_warp->control_points.size() != 4U ||
        live_warp->parameter_bindings.size() != 2U ||
        live_warp->target_slots != std::vector<std::string>{"face"} ||
        std::any_of(
            live_warp->warp_keyforms.begin(),
            live_warp->warp_keyforms.end(),
            [](const marrow::runtime::WarpDeformerKeyform& keyform) {
                return keyform.control_points.size() != 4U;
            })) {
        std::cerr << "Default warp did not expose a complete typed lattice definition.\n";
        return false;
    }
    const std::string warp_typed_before =
        marrow::editor::serialize_project(*state->session.project());
    auto edited_warp = *live_warp;
    edited_warp.name = "Smoke Warp";
    std::swap(
        edited_warp.parameter_bindings[0].parameter,
        edited_warp.parameter_bindings[1].parameter);
    edited_warp.parameter_bindings[0].parameter_index.reset();
    edited_warp.parameter_bindings[1].parameter_index.reset();
    for (auto& keyform : edited_warp.warp_keyforms) {
        std::swap(keyform.x, keyform.y);
    }
    edited_warp.target_slots = {"face"};
    edited_warp.target_slot_indices.clear();
    resize_warp_grid(&edited_warp, 3U, 3U);
    if (!apply_parameter_deformer_definition_edit(
            state,
            "Resize and bind warp deformer smoke",
            std::move(edited_warp))) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    live_warp = state->session.project()->parameter_model->find_deformer(warp_id);
    const Value rebuilt_warp = live_warp == nullptr
        ? object_value()
        : marrow::editor::build_parameter_deformer_authoring_value(*live_warp);
    const Value* rebuilt_warp_marker =
        marrow::runtime::json::find_member(rebuilt_warp, "panel_marker");
    const Value* rebuilt_warp_bindings =
        marrow::runtime::json::find_member(rebuilt_warp, "parameter_bindings");
    const Value* rebuilt_warp_keyforms =
        marrow::runtime::json::find_member(rebuilt_warp, "keyforms");
    const std::string warp_typed_after =
        marrow::editor::serialize_project(*state->session.project());
    if (live_warp == nullptr || live_warp->name != "Smoke Warp" ||
        live_warp->grid_cols != 3U || live_warp->grid_rows != 3U ||
        live_warp->control_points.size() != 9U ||
        live_warp->parameter_bindings.front().parameter != "editor.smoke.axis" ||
        rebuilt_warp_marker == nullptr || !rebuilt_warp_marker->is_string() ||
        rebuilt_warp_marker->as_string() != "warp-top" ||
        rebuilt_warp_bindings == nullptr || !rebuilt_warp_bindings->is_array() ||
        rebuilt_warp_bindings->as_array().empty() ||
        marrow::runtime::json::find_member(
            rebuilt_warp_bindings->as_array().front(), "panel_marker") == nullptr ||
        rebuilt_warp_keyforms == nullptr || !rebuilt_warp_keyforms->is_array() ||
        rebuilt_warp_keyforms->as_array().empty() ||
        marrow::runtime::json::find_member(
            rebuilt_warp_keyforms->as_array().front(), "panel_marker") == nullptr ||
        std::any_of(
            live_warp->warp_keyforms.begin(),
            live_warp->warp_keyforms.end(),
            [](const marrow::runtime::WarpDeformerKeyform& keyform) {
                return keyform.control_points.size() != 9U;
            }) ||
        !state->session.undo() ||
        marrow::editor::serialize_project(*state->session.project()) != warp_typed_before ||
        !state->session.redo() ||
        marrow::editor::serialize_project(*state->session.project()) != warp_typed_after) {
        std::cerr <<
            "Typed warp fields, unknown markers, or undo/redo were not preserved.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    live_warp = state->session.project()->parameter_model->find_deformer(warp_id);
    const auto live_warp_keyform = live_warp == nullptr
        ? std::nullopt
        : current_deformer_keyform_index(*state, warp_id, *live_warp);
    if (!live_warp_keyform.has_value()) {
        std::cerr << "Captured warp keyform was not available to the lattice gesture.\n";
        return false;
    }
    const std::string warp_before =
        marrow::editor::serialize_project(*state->session.project());
    auto warp_gesture = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Warp lattice gesture smoke",
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    const std::string warp_x_field =
        "keyform:" + std::to_string(*live_warp_keyform) + ":0";
    const std::string warp_y_field =
        "keyform:" + std::to_string(*live_warp_keyform) + ":1";
    if (!warp_gesture ||
        !set_parameter_geometry_value(warp_gesture.project(), warp_id, warp_x_field, -19.0) ||
        !warp_gesture.refresh_runtime() ||
        !set_parameter_geometry_value(warp_gesture.project(), warp_id, warp_y_field, -18.0) ||
        !warp_gesture.refresh_runtime()) {
        std::cerr << "Warp lattice gesture did not validate live candidate runtime.\n";
        warp_gesture.cancel();
        return false;
    }
    const auto warp_committed = warp_gesture.commit();
    sync_shell_from_editor_session(state);
    const std::string warp_after =
        marrow::editor::serialize_project(*state->session.project());
    if (!warp_committed || warp_before == warp_after ||
        !state->session.undo() ||
        marrow::editor::serialize_project(*state->session.project()) != warp_before ||
        !state->session.redo() ||
        marrow::editor::serialize_project(*state->session.project()) != warp_after) {
        std::cerr << "Warp lattice drag was not one atomic undoable transaction.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!apply_persistent_parameter_edit(
            state,
            "Delete warp deformer smoke",
            [warp_id](marrow::editor::ProjectData* project) {
                return marrow::editor::delete_parameter_deformer(project, warp_id);
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }

    std::optional<Value> rotation = make_default_rotation_deformer(state, &definition_error);
    if (!rotation.has_value()) {
        std::cerr << definition_error << '\n';
        return false;
    }
    rotation->as_object()["panel_marker"] = string_value("rotation-top");
    Value* rotation_bindings =
        marrow::runtime::json::find_member(*rotation, "parameter_bindings");
    if (rotation_bindings == nullptr || !rotation_bindings->is_array() ||
        rotation_bindings->as_array().empty()) {
        std::cerr << "Default rotation did not expose a marker test binding.\n";
        return false;
    }
    rotation_bindings->as_array().front().as_object()["panel_marker"] =
        string_value("rotation-binding");
    const std::string rotation_id = *raw_id(*rotation);
    if (!apply_persistent_parameter_edit(
            state,
            "Create rotation deformer smoke",
            [definition = std::move(*rotation)](
                marrow::editor::ProjectData* project) mutable {
                return marrow::editor::upsert_parameter_deformer(
                    project, std::move(definition));
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    const auto* created_rotation =
        state->session.project()->parameter_model->find_deformer(rotation_id);
    if (created_rotation == nullptr || created_rotation->name != "Rotation Deformer" ||
        created_rotation->kind != marrow::runtime::ParameterDeformerKind::Rotation ||
        created_rotation->influence != 1.0 || created_rotation->target_slots.size() != 1U ||
        created_rotation->target_slots.front() != "face" ||
        created_rotation->parameter_bindings.size() != 1U ||
        created_rotation->parameter_bindings.front().parameter != "mouth.open" ||
        created_rotation->parameter_bindings.front().axis !=
            marrow::runtime::ParameterDeformerAxis::Angle) {
        std::cerr << "Rotation creation did not select a continuous parameter and mesh slot.\n";
        return false;
    }
    const std::string rotation_typed_before =
        marrow::editor::serialize_project(*state->session.project());
    auto edited_rotation = *created_rotation;
    edited_rotation.influence = 0.75;
    edited_rotation.pivot.x = 0.25F;
    if (!apply_parameter_deformer_definition_edit(
            state,
            "Edit rotation typed fields smoke",
            std::move(edited_rotation))) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    created_rotation =
        state->session.project()->parameter_model->find_deformer(rotation_id);
    const Value rebuilt_rotation = created_rotation == nullptr
        ? object_value()
        : marrow::editor::build_parameter_deformer_authoring_value(*created_rotation);
    const Value* rebuilt_rotation_marker =
        marrow::runtime::json::find_member(rebuilt_rotation, "panel_marker");
    const Value* rebuilt_rotation_bindings =
        marrow::runtime::json::find_member(rebuilt_rotation, "parameter_bindings");
    const std::string rotation_typed_after =
        marrow::editor::serialize_project(*state->session.project());
    if (created_rotation == nullptr || created_rotation->influence != 0.75 ||
        std::abs(created_rotation->pivot.x - 0.25F) > 1e-6F ||
        rebuilt_rotation_marker == nullptr || !rebuilt_rotation_marker->is_string() ||
        rebuilt_rotation_marker->as_string() != "rotation-top" ||
        rebuilt_rotation_bindings == nullptr ||
        !rebuilt_rotation_bindings->is_array() ||
        rebuilt_rotation_bindings->as_array().empty() ||
        marrow::runtime::json::find_member(
            rebuilt_rotation_bindings->as_array().front(), "panel_marker") == nullptr ||
        !state->session.undo() ||
        marrow::editor::serialize_project(*state->session.project()) !=
            rotation_typed_before ||
        !state->session.redo() ||
        marrow::editor::serialize_project(*state->session.project()) !=
            rotation_typed_after) {
        std::cerr <<
            "Typed rotation influence/pivot, markers, or undo/redo were not preserved.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!state->session.set_preview_parameter_value("mouth.open", 0.5)) {
        std::cerr << "Could not position the rotation preview between keyforms.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    const std::string missing_capture_before =
        marrow::editor::serialize_project(*state->session.project());
    auto capture_gesture = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Capture and edit missing rotation keyform smoke",
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!capture_gesture ||
        !marrow::editor::capture_current_deformer_keyform(
            capture_gesture.project(),
            *state->load_result.skeleton_data,
            *state->preview_skeleton,
            rotation_id,
            false) ||
        !set_parameter_geometry_value(
            capture_gesture.project(), rotation_id, "pivot:x", 1.25) ||
        !capture_gesture.refresh_runtime()) {
        std::cerr << "Missing keyform capture and first geometry edit did not validate atomically.\n";
        capture_gesture.cancel();
        return false;
    }
    const auto capture_committed = capture_gesture.commit();
    sync_shell_from_editor_session(state);
    const std::string missing_capture_after =
        marrow::editor::serialize_project(*state->session.project());
    const auto* captured_rotation =
        state->session.project()->parameter_model->find_deformer(rotation_id);
    if (!capture_committed || missing_capture_before == missing_capture_after ||
        captured_rotation == nullptr || captured_rotation->rotation_keyforms.size() != 3U ||
        std::abs(captured_rotation->pivot.x - 1.25) > 1e-6 ||
        !state->session.undo() ||
        marrow::editor::serialize_project(*state->session.project()) !=
            missing_capture_before ||
        !state->session.redo() ||
        marrow::editor::serialize_project(*state->session.project()) !=
            missing_capture_after) {
        std::cerr << "Capture plus geometry edit was not one undoable transaction.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    const std::string rotation_before =
        marrow::editor::serialize_project(*state->session.project());
    auto pivot_gesture = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Rotation pivot gesture smoke",
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!pivot_gesture ||
        !set_parameter_geometry_value(
            pivot_gesture.project(), rotation_id, "pivot:x", 2.0) ||
        !pivot_gesture.refresh_runtime() ||
        !set_parameter_geometry_value(
            pivot_gesture.project(), rotation_id, "pivot:y", -3.0) ||
        !pivot_gesture.refresh_runtime()) {
        std::cerr << "Rotation pivot gesture did not validate live candidate runtime.\n";
        pivot_gesture.cancel();
        return false;
    }
    const auto pivot_committed = pivot_gesture.commit();
    sync_shell_from_editor_session(state);
    const std::string rotation_after =
        marrow::editor::serialize_project(*state->session.project());
    if (!pivot_committed || rotation_before == rotation_after ||
        !state->session.undo() ||
        marrow::editor::serialize_project(*state->session.project()) != rotation_before ||
        !state->session.redo() ||
        marrow::editor::serialize_project(*state->session.project()) != rotation_after) {
        std::cerr << "Rotation pivot drag was not one atomic undoable transaction.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!apply_persistent_parameter_edit(
            state,
            "Delete rotation deformer smoke",
            [rotation_id](marrow::editor::ProjectData* project) {
                return marrow::editor::delete_parameter_deformer(project, rotation_id);
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }

    Value shape = marrow::editor::build_parameter_shape_authoring_value(
        state->session.project()->parameter_model->blend_shapes.front());
    shape.as_object()["id"] = string_value("editor.smoke.shape");
    shape.as_object()["blend_mode"] = string_value("additive_clamped");
    shape.as_object()["panel_marker"] = string_value("shape-top");
    Value* shape_keyforms = marrow::runtime::json::find_member(shape, "keyforms");
    if (shape_keyforms == nullptr || !shape_keyforms->is_array() ||
        shape_keyforms->as_array().empty()) {
        std::cerr << "Shape marker smoke requires one typed keyform.\n";
        return false;
    }
    shape_keyforms->as_array().front().as_object()["panel_marker"] =
        string_value("shape-keyform");
    if (!apply_persistent_parameter_edit(
            state,
            "Create shape smoke",
            [shape = std::move(shape)](marrow::editor::ProjectData* project) mutable {
                return marrow::editor::upsert_parameter_shape(project, std::move(shape));
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    const auto* created_shape =
        state->session.project()->parameter_model->find_shape("editor.smoke.shape");
    if (created_shape == nullptr ||
        created_shape->blend_mode !=
            marrow::runtime::ParameterShapeBlendMode::AdditiveClamped) {
        std::cerr << "Shape blend mode was not preserved by typed authoring.\n";
        return false;
    }
    const std::string shape_typed_before =
        marrow::editor::serialize_project(*state->session.project());
    auto edited_shape = *created_shape;
    edited_shape.parameter = "editor.smoke.axis";
    edited_shape.parameter_index.reset();
    if (!apply_parameter_shape_definition_edit(
            state,
            "Edit shape typed fields smoke",
            std::move(edited_shape))) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    created_shape =
        state->session.project()->parameter_model->find_shape("editor.smoke.shape");
    const Value rebuilt_shape = created_shape == nullptr
        ? object_value()
        : marrow::editor::build_parameter_shape_authoring_value(*created_shape);
    const Value* rebuilt_shape_marker =
        marrow::runtime::json::find_member(rebuilt_shape, "panel_marker");
    const Value* rebuilt_shape_keyforms =
        marrow::runtime::json::find_member(rebuilt_shape, "keyforms");
    const std::string shape_typed_after =
        marrow::editor::serialize_project(*state->session.project());
    if (created_shape == nullptr || created_shape->parameter != "editor.smoke.axis" ||
        created_shape->blend_mode !=
            marrow::runtime::ParameterShapeBlendMode::AdditiveClamped ||
        rebuilt_shape_marker == nullptr || !rebuilt_shape_marker->is_string() ||
        rebuilt_shape_marker->as_string() != "shape-top" ||
        rebuilt_shape_keyforms == nullptr || !rebuilt_shape_keyforms->is_array() ||
        rebuilt_shape_keyforms->as_array().empty() ||
        marrow::runtime::json::find_member(
            rebuilt_shape_keyforms->as_array().front(), "panel_marker") == nullptr ||
        !state->session.undo() ||
        marrow::editor::serialize_project(*state->session.project()) != shape_typed_before ||
        !state->session.redo() ||
        marrow::editor::serialize_project(*state->session.project()) != shape_typed_after) {
        std::cerr << "Typed shape fields, markers, or undo/redo were not preserved.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!apply_persistent_parameter_edit(
            state,
            "Delete shape smoke",
            [](marrow::editor::ProjectData* project) {
                return marrow::editor::delete_parameter_shape(project, "editor.smoke.shape");
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }

    Value expression = make_default_expression(*state->session.project()->parameter_model);
    expression.as_object()["id"] = string_value("editor.smoke.expression");
    expression.as_object()["name"] = string_value("Smoke Expression");
    expression.as_object()["duration"] = number_value(0.2);
    expression.as_object()["blend"] = string_value("override");
    expression.as_object()["priority"] = number_value(7.0);
    expression.as_object()["reset_policy"] = string_value("restore");
    expression.as_object()["panel_marker"] = string_value("expression-top");
    Value* targets = marrow::runtime::json::find_member(expression, "targets");
    targets->as_array().front().as_object()["value"] = number_value(1.0);
    targets->as_array().front().as_object()["panel_marker"] =
        string_value("expression-target");
    targets->as_array().push_back(object_value({
        {"parameter", string_value("editor.smoke.axis")},
        {"value", number_value(0.5)},
    }));
    Value lip_mapping = make_default_lip_mapping(
        *state->session.project()->parameter_model->find_parameter("mouth.open"));
    lip_mapping.as_object()["source"] = string_value("phoneme");
    lip_mapping.as_object()["scale"] = number_value(1.0);
    lip_mapping.as_object()["bias"] = number_value(0.1);
    lip_mapping.as_object()["attack"] = number_value(0.2);
    lip_mapping.as_object()["release"] = number_value(0.2);
    lip_mapping.as_object()["smoothing"] = number_value(0.1);
    lip_mapping.as_object()["panel_marker"] = string_value("lip-top");
    lip_mapping.as_object()["phoneme_map"] = object_value({
        {"AA", number_value(0.8)},
        {"E", number_value(0.4)},
    });
    if (!apply_persistent_parameter_edit(
            state,
            "Create expression and lip mapping smoke",
            [expression = std::move(expression), lip_mapping = std::move(lip_mapping)](
                marrow::editor::ProjectData* project) mutable {
                AuthoringResult result = marrow::editor::upsert_expression(
                    project, std::move(expression));
                if (!result) return result;
                return marrow::editor::upsert_lip_sync_mapping(
                    project, std::move(lip_mapping));
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    const auto* created_expression =
        state->session.project()->parameter_model->find_expression(
            "editor.smoke.expression");
    const auto* created_mapping =
        state->session.project()->parameter_model->find_lip_mapping("mouth.open");
    if (created_expression == nullptr || created_expression->name != "Smoke Expression" ||
        created_expression->duration != 0.2 || created_expression->priority != 7 ||
        created_expression->blend != marrow::runtime::ExpressionBlend::Override ||
        created_expression->reset_policy != marrow::runtime::ExpressionResetPolicy::Restore ||
        created_expression->targets.size() != 2U ||
        created_expression->targets[1].parameter != "editor.smoke.axis" ||
        created_expression->targets[1].value != 0.5 ||
        created_mapping == nullptr ||
        created_mapping->source != marrow::runtime::LipSyncSource::Phoneme ||
        created_mapping->bias != 0.1 || created_mapping->attack != 0.2 ||
        created_mapping->release != 0.2 || created_mapping->smoothing != 0.1 ||
        created_mapping->phoneme_map.size() != 2U) {
        std::cerr << "Expression or lip-sync authoring fields were not preserved.\n";
        return false;
    }

    const std::string expression_typed_before =
        marrow::editor::serialize_project(*state->session.project());
    auto edited_expression = *created_expression;
    edited_expression.targets[1].value = 0.625;
    if (!apply_expression_definition_edit(
            state,
            "Edit expression target smoke",
            std::move(edited_expression))) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    created_expression = state->session.project()->parameter_model->find_expression(
        "editor.smoke.expression");
    const Value rebuilt_expression = created_expression == nullptr
        ? object_value()
        : marrow::editor::build_expression_authoring_value(*created_expression);
    const Value* rebuilt_expression_marker =
        marrow::runtime::json::find_member(rebuilt_expression, "panel_marker");
    const Value* rebuilt_expression_targets =
        marrow::runtime::json::find_member(rebuilt_expression, "targets");
    const std::string expression_typed_after =
        marrow::editor::serialize_project(*state->session.project());
    if (created_expression == nullptr || created_expression->targets[1].value != 0.625 ||
        rebuilt_expression_marker == nullptr ||
        !rebuilt_expression_marker->is_string() ||
        rebuilt_expression_marker->as_string() != "expression-top" ||
        rebuilt_expression_targets == nullptr ||
        !rebuilt_expression_targets->is_array() ||
        rebuilt_expression_targets->as_array().empty() ||
        marrow::runtime::json::find_member(
            rebuilt_expression_targets->as_array().front(), "panel_marker") == nullptr ||
        !state->session.undo() ||
        marrow::editor::serialize_project(*state->session.project()) !=
            expression_typed_before ||
        !state->session.redo() ||
        marrow::editor::serialize_project(*state->session.project()) !=
            expression_typed_after) {
        std::cerr <<
            "Typed expression target, markers, or undo/redo were not preserved.\n";
        return false;
    }
    sync_shell_from_editor_session(state);

    created_mapping =
        state->session.project()->parameter_model->find_lip_mapping("mouth.open");
    const std::string lip_typed_before =
        marrow::editor::serialize_project(*state->session.project());
    auto edited_mapping = *created_mapping;
    edited_mapping.scale = 0.75;
    edited_mapping.phoneme_map.front().value = 0.9;
    if (!apply_lip_sync_mapping_definition_edit(
            state,
            "Edit lip-sync mapping typed fields smoke",
            "mouth.open",
            std::move(edited_mapping))) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    created_mapping =
        state->session.project()->parameter_model->find_lip_mapping("mouth.open");
    const Value rebuilt_mapping = created_mapping == nullptr
        ? object_value()
        : build_lip_mapping_value(*created_mapping);
    const Value* rebuilt_mapping_marker =
        marrow::runtime::json::find_member(rebuilt_mapping, "panel_marker");
    const std::string lip_typed_after =
        marrow::editor::serialize_project(*state->session.project());
    if (created_mapping == nullptr || created_mapping->scale != 0.75 ||
        created_mapping->phoneme_map.empty() ||
        created_mapping->phoneme_map.front().value != 0.9 ||
        rebuilt_mapping_marker == nullptr || !rebuilt_mapping_marker->is_string() ||
        rebuilt_mapping_marker->as_string() != "lip-top" ||
        !state->session.undo() ||
        marrow::editor::serialize_project(*state->session.project()) != lip_typed_before ||
        !state->session.redo() ||
        marrow::editor::serialize_project(*state->session.project()) != lip_typed_after) {
        std::cerr <<
            "Typed lip mapping/phoneme, marker, or undo/redo were not preserved.\n";
        return false;
    }
    sync_shell_from_editor_session(state);

    const bool dirty_before_preview = state->session.dirty();
    const std::string project_before_preview =
        marrow::editor::serialize_project(*state->session.project());
    if (!state->session.set_preview_parameter_value("mouth.open", 0.0) ||
        state->session.dirty() != dirty_before_preview ||
        marrow::editor::serialize_project(*state->session.project()) != project_before_preview ||
        !state->session.set_preview_expression(
            std::optional<std::string>("editor.smoke.expression"))) {
        std::cerr << "Transient parameter preview changed persistent project state.\n";
        return false;
    }
    const auto mouth_index = state->load_result.skeleton_data->find_parameter_index("mouth.open");
    if (!mouth_index.has_value()) return false;
    const double expression_before =
        state->session.preview_skeleton()->parameter_values()[*mouth_index];
    if (!state->session.advance_parameter_state(0.1)) return false;
    const double expression_after =
        state->session.preview_skeleton()->parameter_values()[*mouth_index];
    if (!(expression_after > expression_before && expression_after < 1.0) ||
        state->session.preview_state().time_seconds != time_before) {
        std::cerr << "Paused Parameter mode did not advance expression fade without moving time.\n";
        return false;
    }
    if (!state->session.set_preview_expression(std::nullopt) ||
        !state->session.set_preview_lip_input(1.0, "AA") ||
        !state->session.advance_parameter_state(0.1)) {
        return false;
    }
    const double lip_after =
        state->session.preview_skeleton()->parameter_values()[*mouth_index];
    if (!(lip_after > 0.0 && lip_after < 1.0) ||
        state->session.preview_state().synthetic_phoneme != "AA" ||
        state->session.preview_state().time_seconds != time_before) {
        std::cerr << "Paused Parameter mode did not advance lip filters without moving time.\n";
        return false;
    }

    if (!apply_persistent_parameter_edit(
            state,
            "Delete expression and lip mapping smoke",
            [](marrow::editor::ProjectData* project) {
                AuthoringResult result = marrow::editor::delete_expression(
                    project, "editor.smoke.expression");
                if (!result) return result;
                return marrow::editor::delete_lip_sync_mapping(project, "mouth.open");
            }) ||
        !apply_persistent_parameter_edit(
            state,
            "Delete smoke parameter and group",
            [](marrow::editor::ProjectData* project) {
                AuthoringResult result = marrow::editor::delete_parameter_group(
                    project, "editor.smoke.group");
                if (!result) return result;
                return marrow::editor::delete_parameter(project, "editor.smoke.axis");
            })) {
        std::cerr << state->error_message << '\n';
        return false;
    }
    if (state->session.preview_state().direct_parameter_values.find("editor.smoke.axis") !=
        state->session.preview_state().direct_parameter_values.end()) {
        std::cerr << "Deleted preview parameters were not pruned after runtime rebuild.\n";
        return false;
    }

    state->session.clear_history();
    const int frame_count = options.auto_close_frames.value_or(1);
    for (int frame = 0; frame < frame_count; ++frame) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        (void)state->session.advance_parameter_state(io.DeltaTime);
        sync_shell_from_editor_session_if_revised(state);
        const bool dirty_before_panel = state->session.dirty();
        const std::string project_before_panel =
            marrow::editor::serialize_project(*state->session.project());
        const std::uint64_t project_revision_before_panel =
            state->session.project_revision();
        const std::uint64_t runtime_revision_before_panel =
            state->session.runtime_revision();
        const std::uint64_t preview_revision_before_panel =
            state->session.preview_revision();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0U, viewport);
        ensure_default_dock_layout(state, dockspace_id, viewport);
        draw_parameter_windows(state);
        if (ImGui::FindWindowByName(kParametersWindowTitle) == nullptr ||
            ImGui::FindWindowByName(kParameterDeformersWindowTitle) == nullptr ||
            ImGui::FindWindowByName(kExpressionsWindowTitle) == nullptr ||
            ImGui::FindWindowByName(kLipSyncWindowTitle) == nullptr) {
            std::cerr << "Parameter mode did not create all four authoring panels.\n";
            return false;
        }
        if (state->session.dirty() != dirty_before_panel ||
            marrow::editor::serialize_project(*state->session.project()) !=
                project_before_panel ||
            state->session.project_revision() != project_revision_before_panel ||
            state->session.runtime_revision() != runtime_revision_before_panel ||
            state->session.preview_revision() != preview_revision_before_panel ||
            state->session.can_undo() || state->session.can_redo()) {
            std::cerr <<
                "An input-free parameter panel frame changed project state or history.\n";
            return false;
        }
        ImGui::Render();
    }
    std::cout << "Parameter mode CRUD/capture/preview shell smoke rendered "
              << frame_count << " frame(s).\n";
    return true;
}

} // namespace marrow::editor::shell
