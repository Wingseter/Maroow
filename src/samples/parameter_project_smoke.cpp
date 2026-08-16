#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

#include "marrow/editor/agent_control.hpp"
#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/editor/session.hpp"
#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace {

constexpr double kTolerance = 1e-9;

bool near(double actual, double expected, const char* label) {
    if (std::abs(actual - expected) <= kTolerance) {
        return true;
    }
    std::cerr << label << " expected " << expected << " but got " << actual << ".\n";
    return false;
}

bool require(bool condition, const char* message) {
    if (condition) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

bool validate_runtime_parameter_semantics(
    const std::shared_ptr<const marrow::runtime::SkeletonData>& data) {
    using marrow::runtime::ParameterType;
    if (!require(data->parameters().size() == 2U, "Expected two parameter definitions.") ||
        !require(data->parameter_groups().size() == 1U, "Expected one parameter group.") ||
        !require(data->parameter_shapes().size() == 1U, "Expected one parameter shape.") ||
        !require(
            data->parameters()[1].type == ParameterType::Discrete,
            "Discrete parameter type was not preserved.")) {
        return false;
    }

    marrow::runtime::Skeleton skeleton(data);
    if (!near(skeleton.parameter_values()[0], 0.0, "continuous default") ||
        !near(skeleton.parameter_values()[1], 0.0, "discrete default")) {
        return false;
    }

    const std::uint64_t initial_revision = skeleton.parameter_revision();
    if (!skeleton.set_parameter_value("mouth.open", 2.0) ||
        !near(skeleton.parameter_values()[0], 1.0, "clamped continuous value") ||
        skeleton.parameter_revision() != initial_revision + 1U) {
        std::cerr << "Continuous parameter clamp/revision validation failed.\n";
        return false;
    }
    const std::uint64_t clamped_revision = skeleton.parameter_revision();
    if (!skeleton.set_parameter_value("mouth.open", 2.0) ||
        skeleton.parameter_revision() != clamped_revision) {
        std::cerr << "Equivalent parameter assignment changed the revision.\n";
        return false;
    }
    if (!skeleton.set_parameter_value("face.variant", 7.4) ||
        !near(skeleton.parameter_values()[1], 7.0, "unclamped discrete value")) {
        std::cerr << "Unclamped discrete parameter validation failed.\n";
        return false;
    }
    const std::uint64_t finite_revision = skeleton.parameter_revision();
    if (skeleton.set_parameter_value(
            "mouth.open", std::numeric_limits<double>::quiet_NaN()) ||
        skeleton.parameter_revision() != finite_revision) {
        std::cerr << "Non-finite parameter assignment was not rejected atomically.\n";
        return false;
    }

    skeleton.reset_parameters();
    const auto* animation = data->find_animation("idle");
    const auto slot_index = data->find_slot_index("face");
    if (animation == nullptr || !slot_index.has_value()) {
        std::cerr << "Parameter fixture animation or target slot is missing.\n";
        return false;
    }
    skeleton.apply_animation(*animation, 0.5);
    const std::vector<double>* animation_offsets =
        skeleton.current_mesh_vertex_offsets(*slot_index);
    if (animation_offsets == nullptr || animation_offsets->size() != 8U ||
        !near((*animation_offsets)[2], 0.5, "animation FFD offset")) {
        std::cerr << "Animation-only FFD accessor did not preserve its contract.\n";
        return false;
    }
    if (!skeleton.set_parameter_value("mouth.open", 0.5)) {
        return false;
    }
    const std::vector<double>* final_offsets =
        skeleton.current_final_mesh_vertex_offsets(*slot_index);
    if (final_offsets == nullptr || final_offsets->size() != 8U ||
        !near((*final_offsets)[1], -2.0, "override shape lower offset") ||
        !near((*final_offsets)[2], 0.0, "override shape replaced animation FFD") ||
        !near((*final_offsets)[5], 4.0, "override shape upper offset")) {
        std::cerr << "Final mesh offset composition did not apply normalized override.\n";
        return false;
    }
    return true;
}

bool validate_preview_history(const std::filesystem::path& project_path) {
    marrow::editor::EditorSession session;
    const auto opened = session.open(project_path);
    if (!opened) {
        std::cerr << opened.error->format() << '\n';
        return false;
    }
    const std::string persistent_before =
        marrow::editor::serialize_project(*session.project());
    const bool dirty_before = session.dirty();
    const auto changed = session.set_preview_parameter_value(
        "mouth.open",
        0.75,
        {marrow::editor::EditKind::PreviewComposition,
         "Set parameter in smoke",
         "parameter-smoke",
         false,
         marrow::editor::EditImpact::Preview});
    if (!changed || !changed.changed || session.dirty() != dirty_before ||
        marrow::editor::serialize_project(*session.project()) != persistent_before ||
        session.preview_skeleton() == nullptr ||
        !near(session.preview_skeleton()->direct_parameter_values()[0], 0.75, "preview direct") ||
        !near(session.preview_skeleton()->parameter_values()[0], 0.75, "preview final")) {
        std::cerr << "Preview-only parameter edit changed persistent project state.\n";
        return false;
    }
    const auto undone = session.undo();
    if (!undone || !undone.changed || session.preview_skeleton() == nullptr ||
        !near(session.preview_skeleton()->parameter_values()[0], 0.0, "preview undo")) {
        std::cerr << "Preview parameter undo failed.\n";
        return false;
    }
    const auto redone = session.redo();
    if (!redone || !redone.changed || session.preview_skeleton() == nullptr ||
        !near(session.preview_skeleton()->parameter_values()[0], 0.75, "preview redo")) {
        std::cerr << "Preview parameter redo failed.\n";
        return false;
    }

    const auto variant_index =
        session.runtime_data()->find_parameter_index("face.variant");
    const auto raw_variant = session.set_preview_parameter_value(
        "face.variant",
        7.4,
        {marrow::editor::EditKind::PreviewComposition,
         "Set raw discrete parameter in smoke",
         "parameter-raw-discrete-smoke",
         false,
         marrow::editor::EditImpact::Preview});
    const auto raw_discrete_is_preserved = [&]() {
        if (!variant_index.has_value() || session.preview_skeleton() == nullptr) {
            return false;
        }
        const auto direct =
            session.preview_state().direct_parameter_values.find("face.variant");
        return direct != session.preview_state().direct_parameter_values.end() &&
            near(direct->second, 7.4, "raw discrete preview state") &&
            near(
                session.preview_skeleton()->direct_parameter_values()[*variant_index],
                7.4,
                "raw discrete skeleton input") &&
            near(
                session.preview_skeleton()->parameter_values()[*variant_index],
                7.0,
                "composed discrete skeleton value");
    };
    if (!raw_variant || !raw_variant.changed || !raw_discrete_is_preserved()) {
        std::cerr << "Discrete preview input was normalized before composition.\n";
        return false;
    }

    const std::string before_failed_edit =
        marrow::editor::serialize_project(*session.project());
    const bool before_failed_dirty = session.dirty();
    const std::uint64_t before_failed_project_revision = session.project_revision();
    const std::uint64_t before_failed_runtime_revision = session.runtime_revision();
    const std::uint64_t before_failed_preview_revision = session.preview_revision();
    const std::size_t before_failed_undo_count = session.undo_count();
    auto transaction = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Invalid parameter deformer smoke",
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction || transaction.project() == nullptr ||
        !transaction.project()->parameter_model.has_value()) {
        std::cerr << "Could not begin failed parameter-model transaction smoke.\n";
        return false;
    }
    marrow::editor::ParameterDeformerAuthoringDefinition invalid_deformer;
    invalid_deformer.id = "invalid.deformer";
    invalid_deformer.name = "Invalid Deformer";
    transaction.project()->parameter_model->deformers.push_back(
        std::move(invalid_deformer));
    const auto rejected = transaction.commit();
    if (rejected || session.transaction_active() ||
        marrow::editor::serialize_project(*session.project()) != before_failed_edit ||
        session.dirty() != before_failed_dirty ||
        session.project_revision() != before_failed_project_revision ||
        session.runtime_revision() != before_failed_runtime_revision ||
        session.preview_revision() != before_failed_preview_revision ||
        session.undo_count() != before_failed_undo_count ||
        session.preview_skeleton() == nullptr ||
        !near(session.preview_skeleton()->parameter_values()[0], 0.75, "failed edit rollback") ||
        !raw_discrete_is_preserved()) {
        std::cerr << "Invalid parameter-model transaction did not roll back atomically.\n";
        return false;
    }

    auto create_parameter = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Create preview normalization parameter",
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!create_parameter || create_parameter.project() == nullptr ||
        !create_parameter.project()->parameter_model.has_value()) {
        std::cerr << "Could not begin parameter preview normalization smoke.\n";
        return false;
    }
    marrow::editor::ParameterAuthoringDefinition added_parameter;
    added_parameter.id = "smoke.extra";
    added_parameter.name = "Smoke Extra";
    added_parameter.default_value = 0.25;
    create_parameter.project()->parameter_model->parameters.push_back(
        std::move(added_parameter));
    const auto created = create_parameter.commit();
    const auto& created_values = session.preview_state().direct_parameter_values;
    if (!created || created_values.find("mouth.open") == created_values.end() ||
        !near(created_values.at("mouth.open"), 0.75, "preserved preview id") ||
        created_values.find("smoke.extra") == created_values.end() ||
        !near(created_values.at("smoke.extra"), 0.25, "new preview default") ||
        !raw_discrete_is_preserved()) {
        std::cerr << "Runtime rebuild did not preserve/add preview parameter ids correctly.\n";
        return false;
    }
    const auto removed_by_undo = session.undo();
    if (!removed_by_undo ||
        session.preview_state().direct_parameter_values.find("smoke.extra") !=
            session.preview_state().direct_parameter_values.end() ||
        !near(
            session.preview_state().direct_parameter_values.at("mouth.open"),
            0.75,
            "preview id after prune") ||
        !raw_discrete_is_preserved()) {
        std::cerr << "Runtime rebuild did not prune a deleted preview parameter id.\n";
        return false;
    }
    const auto restored_by_redo = session.redo();
    if (!restored_by_redo ||
        session.preview_state().direct_parameter_values.find("smoke.extra") ==
            session.preview_state().direct_parameter_values.end() ||
        !near(
            session.preview_state().direct_parameter_values.at("smoke.extra"),
            0.25,
            "preview id after redo") ||
        !raw_discrete_is_preserved()) {
        std::cerr << "Runtime rebuild did not restore a new parameter default on redo.\n";
        return false;
    }
    return true;
}

bool append_fixture_parameters(
    std::string_view fixture_path,
    marrow::editor::ParameterModel* model) {
    const auto loaded = marrow::runtime::load_skeleton_data(fixture_path);
    if (!loaded) {
        std::cerr << loaded.error->format() << '\n';
        return false;
    }
    for (const marrow::runtime::ParameterDefinition& source :
         loaded.skeleton_data->parameters()) {
        if (model->find_parameter(source.id) != nullptr) continue;
        marrow::editor::ParameterAuthoringDefinition parameter;
        parameter.id = source.id;
        parameter.name = source.name;
        parameter.min_value = source.min_value;
        parameter.max_value = source.max_value;
        parameter.default_value = source.default_value;
        parameter.type = source.type == marrow::runtime::ParameterType::Continuous
            ? marrow::editor::ParameterAuthoringType::Continuous
            : marrow::editor::ParameterAuthoringType::Discrete;
        parameter.clamp = source.clamp;
        parameter.ui_step = source.ui_step;
        parameter.units = source.units;
        model->parameters.push_back(std::move(parameter));
    }
    return true;
}

const marrow::runtime::json::Value* fixture_root_member(
    const marrow::runtime::json::Document& document,
    std::string_view key) {
    return marrow::runtime::json::find_member(document.root, key);
}

marrow::runtime::json::Value marker_value(std::string value) {
    return marrow::runtime::json::Value(std::move(value), {});
}

bool remove_marker(
    marrow::runtime::json::Value* value,
    std::string_view key) {
    if (value == nullptr || !value->is_object()) return false;
    value->as_object().erase(std::string(key));
    return true;
}

bool mark_first_array_entry(
    marrow::runtime::json::Value* value,
    std::string_view array_key,
    std::string marker_key) {
    if (value == nullptr || !value->is_object()) return false;
    auto* values = marrow::runtime::json::find_member(*value, array_key);
    if (values == nullptr || !values->is_array() || values->as_array().empty() ||
        !values->as_array().front().is_object()) {
        return false;
    }
    values->as_array().front().as_object()[std::move(marker_key)] =
        marker_value("keep");
    return true;
}

bool remove_first_array_marker(
    marrow::runtime::json::Value* value,
    std::string_view array_key,
    std::string_view marker_key) {
    if (value == nullptr || !value->is_object()) return false;
    auto* values = marrow::runtime::json::find_member(*value, array_key);
    return values != nullptr && values->is_array() && !values->as_array().empty() &&
        remove_marker(&values->as_array().front(), marker_key);
}

bool exercise_lossless_typed_rewrites(marrow::editor::ProjectData* project) {
    if (project == nullptr || !project->parameter_model.has_value()) return false;
    auto& model = *project->parameter_model;
    if (model.blend_shapes.empty() || model.deformers.empty() || model.art_paths.empty() ||
        model.expressions.empty() || model.lip_sync.mappings.empty()) {
        return false;
    }

    auto& shape = model.blend_shapes.front();
    shape.preserved_source.as_object()["future_shape"] = marker_value("keep");
    if (!mark_first_array_entry(
            &shape.preserved_source, "keyforms", "future_shape_keyform")) {
        return false;
    }
    auto shape_edit = marrow::editor::build_parameter_shape_authoring_value(shape);
    remove_marker(&shape_edit, "future_shape");
    remove_first_array_marker(&shape_edit, "keyforms", "future_shape_keyform");
    shape_edit.as_object()["blend_mode"] = marker_value("additive_clamped");
    if (!marrow::editor::upsert_parameter_shape(
            project, std::move(shape_edit), true)) {
        return false;
    }

    auto& deformer = model.deformers.front();
    deformer.preserved_source.as_object()["future_deformer"] = marker_value("keep");
    if (!mark_first_array_entry(
            &deformer.preserved_source, "parameter_bindings", "future_binding") ||
        !mark_first_array_entry(
            &deformer.preserved_source, "keyforms", "future_deformer_keyform")) {
        return false;
    }
    auto deformer_edit = marrow::editor::build_parameter_deformer_authoring_value(deformer);
    remove_marker(&deformer_edit, "future_deformer");
    remove_first_array_marker(&deformer_edit, "parameter_bindings", "future_binding");
    remove_first_array_marker(&deformer_edit, "keyforms", "future_deformer_keyform");
    deformer_edit.as_object()["name"] = marker_value("Face Warp Typed");
    if (!marrow::editor::upsert_parameter_deformer(
            project, std::move(deformer_edit), true)) {
        return false;
    }

    auto& art_path = model.art_paths.front();
    art_path.preserved_source.as_object()["future_art_path"] = marker_value("keep");
    auto* parameter_keyforms = marrow::runtime::json::find_member(
        art_path.preserved_source, "parameter_keyforms");
    if (parameter_keyforms == nullptr ||
        !mark_first_array_entry(
            parameter_keyforms, "keyforms", "future_art_keyform")) {
        return false;
    }
    art_path.name = "Brow Stroke Typed";

    auto& expression = model.expressions.front();
    expression.preserved_source.as_object()["future_expression"] = marker_value("keep");
    if (!mark_first_array_entry(
            &expression.preserved_source, "targets", "future_expression_target")) {
        return false;
    }
    auto expression_edit = marrow::editor::build_expression_authoring_value(expression);
    remove_marker(&expression_edit, "future_expression");
    remove_first_array_marker(
        &expression_edit, "targets", "future_expression_target");
    expression_edit.as_object()["priority"] =
        marrow::runtime::json::Value(
            static_cast<double>(expression.priority + 1), {});
    if (!marrow::editor::upsert_expression(
            project, std::move(expression_edit), true)) {
        return false;
    }

    model.lip_sync.preserved_source.as_object()["future_lip_sync"] = marker_value("keep");
    auto& mapping = model.lip_sync.mappings.front();
    mapping.preserved_source.as_object()["future_lip_mapping"] = marker_value("keep");
    marrow::editor::LipSyncAuthoringDefinition mapping_section;
    mapping_section.mappings.push_back(mapping);
    auto mapping_section_value =
        marrow::editor::build_lip_sync_authoring_value(mapping_section);
    auto* mapping_values = marrow::runtime::json::find_member(
        mapping_section_value, "mappings");
    if (mapping_values == nullptr || !mapping_values->is_array() ||
        mapping_values->as_array().empty()) {
        return false;
    }
    auto mapping_edit = mapping_values->as_array().front();
    remove_marker(&mapping_edit, "future_lip_mapping");
    mapping_edit.as_object()["scale"] =
        marrow::runtime::json::Value(mapping.scale + 0.1, {});
    if (!marrow::editor::upsert_lip_sync_mapping(
            project, std::move(mapping_edit))) {
        return false;
    }
    return true;
}

bool populate_complete_parameter_model(marrow::editor::ProjectData* project) {
    if (project == nullptr || !project->parameter_model.has_value()) return false;
    marrow::editor::ParameterModel& model = *project->parameter_model;
    if (!append_fixture_parameters("assets/fixtures/parameter_deformer_grid.mskl", &model) ||
        !append_fixture_parameters("assets/fixtures/art_path_stroke.mskl", &model) ||
        !append_fixture_parameters("assets/fixtures/parameter_expression_lipsync.mskl", &model)) {
        return false;
    }

    const auto deformer_document = marrow::runtime::json::load_document(
        "assets/fixtures/parameter_deformer_grid.mskl");
    const auto art_path_document = marrow::runtime::json::load_document(
        "assets/fixtures/art_path_stroke.mskl");
    const auto composition_document = marrow::runtime::json::load_document(
        "assets/fixtures/parameter_expression_lipsync.mskl");
    if (!deformer_document || !art_path_document || !composition_document) {
        std::cerr << "Could not load complete parameter-model fixture sections.\n";
        return false;
    }

    const auto parse_array = [](const marrow::runtime::json::Value* source,
                                auto* output,
                                const auto& parse) {
        if (source == nullptr || !source->is_array()) return false;
        output->clear();
        for (const marrow::runtime::json::Value& value : source->as_array()) {
            typename std::decay_t<decltype(*output)>::value_type definition;
            std::string error;
            if (!parse(value, &definition, &error)) {
                std::cerr << error << '\n';
                return false;
            }
            output->push_back(std::move(definition));
        }
        return true;
    };
    const auto* deformers = fixture_root_member(
        *deformer_document.document, "parameterDeformers");
    const auto* art_paths = fixture_root_member(*art_path_document.document, "artPaths");
    const auto* expressions = fixture_root_member(
        *composition_document.document, "expressions");
    const auto* lip_sync = fixture_root_member(*composition_document.document, "lipSync");
    std::string lip_error;
    if (!parse_array(
            deformers,
            &model.deformers,
            marrow::editor::parse_parameter_deformer_authoring_value) ||
        !parse_array(
            art_paths,
            &model.art_paths,
            marrow::editor::parse_art_path_authoring_value) ||
        !parse_array(
            expressions,
            &model.expressions,
            marrow::editor::parse_expression_authoring_value) ||
        lip_sync == nullptr || !lip_sync->is_object() ||
        !marrow::editor::parse_lip_sync_authoring_value(
            *lip_sync, &model.lip_sync, &lip_error)) {
        std::cerr << "Complete parameter-model fixture sections have unexpected wire types.\n";
        if (!lip_error.empty()) std::cerr << lip_error << '\n';
        return false;
    }
    return true;
}

bool validate_complete_project_round_trip(
    const marrow::editor::ProjectData& source,
    const marrow::runtime::json::Document& base_skeleton,
    marrow::editor::ProjectData* round_tripped_out) {
    marrow::editor::ProjectData save_source = source;
    save_source.runtime_assets.skeleton_path =
        std::filesystem::absolute(source.resolved_skeleton_path());
    for (std::filesystem::path& atlas_path : save_source.runtime_assets.atlas_paths) {
        atlas_path = std::filesystem::absolute(source.resolve_path(atlas_path));
    }
    const std::filesystem::path save_path =
        "/tmp/marrow_parameter_complete_typed.marrow";
    const auto saved = marrow::editor::save_project(save_source, save_path);
    if (!saved) {
        std::cerr << saved.error->format() << '\n';
        return false;
    }
    const auto reloaded = marrow::editor::load_project(save_path);
    if (!reloaded || !reloaded.project->parameter_model.has_value()) {
        std::cerr << (reloaded.error.has_value()
                          ? reloaded.error->format()
                          : "Complete parameter model did not reload.")
                  << '\n';
        return false;
    }
    const marrow::editor::ParameterModel& model = *reloaded.project->parameter_model;
    if (model.blend_shapes.size() != 1U || model.deformers.size() != 2U ||
        model.art_paths.size() != 2U || model.expressions.size() != 2U ||
        model.lip_sync.mappings.size() != 2U ||
        model.blend_shapes.front().parameter != "mouth.open" ||
        model.blend_shapes.front().keyforms.size() != 2U ||
        model.deformers.front().kind != marrow::runtime::ParameterDeformerKind::Warp ||
        model.deformers.front().warp_keyforms.size() != 4U ||
        model.deformers[1].kind != marrow::runtime::ParameterDeformerKind::Rotation ||
        model.art_paths.front().parameter_keyforms == std::nullopt ||
        model.expressions.front().targets.size() != 2U ||
        model.lip_sync.mappings[1].source != marrow::runtime::LipSyncSource::Phoneme ||
        model.lip_sync.mappings[1].phoneme_map.size() != 3U) {
        std::cerr << "A promoted parameter-model section was lost on project reload.\n";
        return false;
    }
    const std::string serialized = marrow::editor::serialize_project(*reloaded.project);
    for (std::string_view marker : {
             "future_shape",
             "future_shape_keyform",
             "future_deformer",
             "future_binding",
             "future_deformer_keyform",
             "future_art_path",
             "future_art_keyform",
             "future_expression",
             "future_expression_target",
             "future_lip_sync",
             "future_lip_mapping"}) {
        if (serialized.find(marker) == std::string::npos) {
            std::cerr << "Typed known-field rewrite lost unknown marker: " << marker << '\n';
            return false;
        }
    }
    const auto runtime = marrow::editor::build_project_runtime(
        *reloaded.project, base_skeleton);
    if (!runtime || runtime.skeleton_data->parameter_deformers().size() != 2U ||
        runtime.skeleton_data->art_paths().size() != 2U ||
        runtime.skeleton_data->expressions().size() != 2U ||
        runtime.skeleton_data->lip_sync().mappings.size() != 2U) {
        std::cerr << (runtime.error.has_value()
                          ? runtime.error->format()
                          : "Complete parameter-model runtime build lost a typed section.")
                  << '\n';
        return false;
    }
    *round_tripped_out = *reloaded.project;
    return true;
}

bool validate_typed_family_rollbacks(const std::filesystem::path& project_path) {
    marrow::editor::EditorSession session;
    const auto opened = session.open(project_path);
    if (!opened) {
        std::cerr << opened.error->format() << '\n';
        return false;
    }
    const auto reject = [&](std::string label, const auto& mutate) {
        const std::string before = marrow::editor::serialize_project(*session.project());
        const bool dirty_before = session.dirty();
        const std::uint64_t project_revision = session.project_revision();
        const std::uint64_t runtime_revision = session.runtime_revision();
        const std::uint64_t preview_revision = session.preview_revision();
        const std::size_t undo_count = session.undo_count();
        auto transaction = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            std::move(label),
            {},
            false,
            marrow::editor::EditImpact::Project |
                marrow::editor::EditImpact::Runtime |
                marrow::editor::EditImpact::Preview});
        if (!transaction || transaction.project() == nullptr ||
            !transaction.project()->parameter_model.has_value()) {
            return false;
        }
        mutate(*transaction.project()->parameter_model);
        const auto result = transaction.commit();
        return !result && !session.transaction_active() &&
            marrow::editor::serialize_project(*session.project()) == before &&
            session.dirty() == dirty_before &&
            session.project_revision() == project_revision &&
            session.runtime_revision() == runtime_revision &&
            session.preview_revision() == preview_revision &&
            session.undo_count() == undo_count;
    };

    if (!reject("Reject invalid typed shape", [](marrow::editor::ParameterModel& model) {
            model.blend_shapes.front().keyforms.clear();
        }) ||
        !reject("Reject invalid typed deformer", [](marrow::editor::ParameterModel& model) {
            model.deformers.front().parameter_bindings.clear();
        }) ||
        !reject("Reject invalid typed ArtPath", [](marrow::editor::ParameterModel& model) {
            model.art_paths.front().width = 0.0;
        }) ||
        !reject("Reject invalid typed expression", [](marrow::editor::ParameterModel& model) {
            model.expressions.front().targets.clear();
        }) ||
        !reject("Reject invalid typed lip mapping", [](marrow::editor::ParameterModel& model) {
            model.lip_sync.mappings.front().attack = -1.0;
        })) {
        std::cerr << "A typed parameter-model family failed atomic candidate rollback.\n";
        return false;
    }
    return true;
}

bool validate_composed_agent_parameter_parity(
    const std::filesystem::path& project_path) {
    marrow::editor::EditorSession session;
    const auto opened = session.open(project_path);
    if (!opened) {
        std::cerr << opened.error->format() << '\n';
        return false;
    }
    if (!session.set_preview_expression(std::optional<std::string>("smile")) ||
        !session.set_preview_lip_input(0.0, "E") ||
        !session.advance_parameter_state(0.1)) {
        std::cerr << "Could not prepare composed expression/lip preview state.\n";
        return false;
    }
    const auto parameter_index =
        session.runtime_data()->find_parameter_index("mouth.form");
    if (!parameter_index.has_value() || session.preview_skeleton() == nullptr ||
        !near(
            session.preview_skeleton()->parameter_values()[*parameter_index],
            1.0,
            "prepared composed parameter")) {
        return false;
    }

    const std::string project_before =
        marrow::editor::serialize_project(*session.project());
    const bool dirty_before = session.dirty();
    const std::uint64_t project_revision_before = session.project_revision();
    const std::uint64_t runtime_revision_before = session.runtime_revision();
    const std::uint64_t preview_revision_before = session.preview_revision();
    const std::size_t undo_before = session.undo_count();
    const std::size_t redo_before = session.redo_count();
    const auto preview_inputs_before = session.preview_state().direct_parameter_values;
    const auto skeleton_direct_before =
        session.preview_skeleton()->direct_parameter_values();
    const auto skeleton_final_before = session.preview_skeleton()->parameter_values();

    marrow::editor::AgentControlState control;
    const auto dispatch = [&](std::string_view command) {
        const auto parsed = marrow::runtime::json::parse_document(command);
        if (!parsed || !parsed.document->root.is_object()) {
            return marrow::editor::AgentDispatchResult{};
        }
        marrow::editor::AgentCommandContext context{session, control};
        return marrow::editor::AgentCommandDispatcher::dispatch(
            context, parsed.document->root);
    };
    const auto applied_value = [](const marrow::editor::AgentDispatchResult& result) {
        const auto* applied = result.scene_delta.is_object()
            ? marrow::runtime::json::find_member(result.scene_delta, "applied")
            : nullptr;
        return applied != nullptr && applied->is_number()
            ? std::optional<double>(applied->as_number())
            : std::nullopt;
    };

    const auto dry = dispatch(
        R"json({"op":"parameter.set","args":{"id":"mouth.form","value":-0.25,"dry_run":true}})json");
    if (!dry.ok || applied_value(dry) != std::optional<double>(1.0) ||
        marrow::editor::serialize_project(*session.project()) != project_before ||
        session.dirty() != dirty_before ||
        session.project_revision() != project_revision_before ||
        session.runtime_revision() != runtime_revision_before ||
        session.preview_revision() != preview_revision_before ||
        session.undo_count() != undo_before || session.redo_count() != redo_before ||
        session.preview_state().direct_parameter_values != preview_inputs_before ||
        session.preview_skeleton()->direct_parameter_values() != skeleton_direct_before ||
        session.preview_skeleton()->parameter_values() != skeleton_final_before) {
        std::cerr << "Composed parameter dry-run changed session state or reported the wrong value.\n";
        return false;
    }

    const auto live = dispatch(
        R"json({"op":"parameter.set","args":{"id":"mouth.form","value":-0.25}})json");
    const auto live_direct =
        session.preview_state().direct_parameter_values.find("mouth.form");
    if (!live.ok || applied_value(live) != applied_value(dry) ||
        applied_value(live) != std::optional<double>(1.0) ||
        live_direct == session.preview_state().direct_parameter_values.end() ||
        !near(live_direct->second, -0.25, "live composed direct input") ||
        !near(
            session.preview_skeleton()->direct_parameter_values()[*parameter_index],
            -0.25,
            "live composed skeleton input") ||
        !near(
            session.preview_skeleton()->parameter_values()[*parameter_index],
            1.0,
            "live composed final value")) {
        std::cerr << "Composed parameter dry/live dispatch results diverged.\n";
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path project_path = argc > 1
        ? std::filesystem::path(argv[1])
        : std::filesystem::path("assets/fixtures/parameter_face_basic.marrow");
    const auto loaded = marrow::editor::load_project(project_path);
    if (!loaded) {
        std::cerr << loaded.error->format() << '\n';
        return 1;
    }
    if (!require(loaded.project->parameter_model.has_value(), "Parameter model was not loaded.") ||
        !require(
            loaded.project->parameter_model->parameters.size() == 2U,
            "Project parameter definitions were not typed.") ||
        !require(
            loaded.project->parameter_model->groups.size() == 1U,
            "Project parameter groups were not typed.")) {
        return 1;
    }

    const std::string serialized = marrow::editor::serialize_project(*loaded.project);
    if (serialized.find("\"future_metadata\"") == std::string::npos ||
        serialized.find("\"authoring_hint\"") == std::string::npos) {
        std::cerr << "Unknown additive parameter-model fields were not preserved.\n";
        return 1;
    }

    const auto old_project =
        marrow::editor::load_project("assets/fixtures/player_idle.marrow");
    if (!old_project ||
        marrow::editor::serialize_project(*old_project.project).find("\"parameter_model\"") !=
            std::string::npos) {
        std::cerr << "A legacy project gained a serialized empty parameter model.\n";
        return 1;
    }

    const auto runtime = marrow::editor::build_project_runtime(
        *loaded.project, *loaded.base_skeleton_document);
    if (!runtime) {
        std::cerr << runtime.error->format() << '\n';
        return 1;
    }
    if (!validate_runtime_parameter_semantics(runtime.skeleton_data) ||
        !validate_preview_history(project_path)) {
        return 1;
    }

    marrow::editor::ProjectData complete_project = *loaded.project;
    if (!populate_complete_parameter_model(&complete_project) ||
        !exercise_lossless_typed_rewrites(&complete_project)) {
        std::cerr << "Could not exercise lossless typed parameter-model rewrites.\n";
        return 1;
    }
    marrow::editor::ProjectData round_tripped_project;
    if (!validate_complete_project_round_trip(
            complete_project,
            *loaded.base_skeleton_document,
            &round_tripped_project)) {
        return 1;
    }
    if (!validate_typed_family_rollbacks(
            "/tmp/marrow_parameter_complete_typed.marrow") ||
        !validate_composed_agent_parameter_parity(
            "/tmp/marrow_parameter_complete_typed.marrow")) {
        return 1;
    }

    marrow::editor::ProjectExportOptions export_options;
    export_options.skeleton_output_path = "/tmp/marrow_parameter_face_basic.mskl";
    export_options.binary_output_path = "/tmp/marrow_parameter_face_basic.mbin";
    const auto exported = marrow::editor::export_runtime_assets(
        round_tripped_project, *loaded.base_skeleton_document, export_options);
    if (!exported || !exported.binary_path.has_value()) {
        std::cerr << (exported.error.has_value()
                          ? exported.error->format()
                          : "Parameter project binary export did not produce an output.")
                  << '\n';
        return 1;
    }
    const auto exported_json = marrow::runtime::load_skeleton_data(exported.path);
    const auto exported_binary = marrow::runtime::load_skeleton_data(*exported.binary_path);
    if (!exported_json || !exported_binary ||
        exported_json.skeleton_data->parameters().size() !=
            round_tripped_project.parameter_model->parameters.size() ||
        exported_binary.skeleton_data->parameters().size() !=
            round_tripped_project.parameter_model->parameters.size() ||
        exported_binary.skeleton_data->parameter_shapes().size() != 1U ||
        exported_binary.skeleton_data->parameter_deformers().size() != 2U ||
        exported_binary.skeleton_data->art_paths().size() != 2U ||
        exported_binary.skeleton_data->expressions().size() != 2U ||
        exported_binary.skeleton_data->lip_sync().mappings.size() != 2U) {
        std::cerr << "Exported JSON/binary parameter models did not round-trip.\n";
        return 1;
    }

    std::cout << "Parameter project/runtime/preview/JSON-binary smoke passed.\n";
    return 0;
}
