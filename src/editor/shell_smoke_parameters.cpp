#include "shell_smoke_scenarios.hpp"

#include <array>
#include <cmath>
#include <iostream>
#include <string>
#include <utility>

#include "imgui.h"
#include "imgui_internal.h"

#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
#include "shell_parameter_actions.hpp"
#include "shell_parameters.hpp"
#include "shell_project_panels.hpp"

namespace marrow::editor::shell {

using marrow::editor::AuthoringResult;
using marrow::editor::ParameterAuthoringDefinition;
using marrow::runtime::json::Value;
using namespace parameter_panel;

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
