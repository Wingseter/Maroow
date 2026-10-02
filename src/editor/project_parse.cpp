#include "marrow/editor/project.hpp"
#include "project_json.hpp"
#include "project_internal.hpp"
#include "mesh_weight_model.hpp"
#include "marrow/editor/authoring.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <type_traits>
#include <utility>

namespace marrow::editor {
using namespace project_detail;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

namespace {

std::optional<LoadError> read_required_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    std::string* value_out) {
    const Value* member = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, object, key, Value::Type::String, json_path, &member)) {
        return error;
    }

    *value_out = member->as_string();
    return std::nullopt;
}

std::optional<LoadError> read_optional_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    std::string* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::String, member_path)) {
        return error;
    }

    *value_out = member->as_string();
    return std::nullopt;
}

template <typename Number>
std::optional<LoadError> assign_number(
    const Document& document,
    const Value& value,
    std::string_view json_path,
    Number* value_out) {
    const double parsed = value.as_number();
    if constexpr (std::is_same_v<Number, double>) {
        *value_out = parsed;
        return std::nullopt;
    } else {
        if (!std::isfinite(parsed) ||
            parsed < -static_cast<double>(std::numeric_limits<Number>::max()) ||
            parsed > static_cast<double>(std::numeric_limits<Number>::max())) {
            return validation_error(
                document,
                value.location(),
                std::string(json_path),
                "number is outside the runtime float32 range");
        }

        *value_out = static_cast<Number>(parsed);
        return std::nullopt;
    }
}

template <typename Number>
std::optional<LoadError> read_required_number(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    Number* value_out) {
    const Value* member = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, object, key, Value::Type::Number, json_path, &member)) {
        return error;
    }

    return assign_number(
        document,
        *member,
        std::string(json_path) + "." + std::string(key),
        value_out);
}

template <typename Number>
std::optional<LoadError> read_optional_number(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    Number* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::Number, member_path)) {
        return error;
    }

    return assign_number(document, *member, member_path, value_out);
}

std::optional<LoadError> read_optional_boolean(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    bool* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::Boolean, member_path)) {
        return error;
    }

    *value_out = member->as_boolean();
    return std::nullopt;
}

std::optional<LoadError> read_optional_integer(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    int* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::Number, member_path)) {
        return error;
    }

    const double raw_value = member->as_number();
    const double rounded_value = std::round(raw_value);
    if (std::abs(raw_value - rounded_value) > 1e-6) {
        return validation_error(
            document,
            member->location(),
            member_path,
            "integer values must be whole numbers");
    }

    *value_out = static_cast<int>(rounded_value);
    return std::nullopt;
}

std::optional<LoadError> parse_string_array(
    const Document& document,
    const Value& array_value,
    std::string_view json_path,
    std::vector<std::string>* values_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, array_value, Value::Type::Array, json_path)) {
        return error;
    }

    std::vector<std::string> values;
    values.reserve(array_value.as_array().size());

    for (std::size_t index = 0; index < array_value.as_array().size(); ++index) {
        const Value& entry = array_value.as_array()[index];
        const std::string entry_path =
            std::string(json_path) + "[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, entry, Value::Type::String, entry_path)) {
            return error;
        }
        if (entry.as_string().empty()) {
            return validation_error(
                document,
                entry.location(),
                entry_path,
                "path strings must not be empty");
        }

        values.push_back(entry.as_string());
    }

    *values_out = std::move(values);
    return std::nullopt;
}

std::optional<LoadError> parse_path_array(
    const Document& document,
    const Value& array_value,
    std::string_view json_path,
    std::vector<std::filesystem::path>* paths_out) {
    std::vector<std::string> values;
    if (const auto error = parse_string_array(document, array_value, json_path, &values)) {
        return error;
    }

    std::vector<std::filesystem::path> paths;
    paths.reserve(values.size());
    for (const std::string& value : values) {
        paths.emplace_back(value);
    }

    *paths_out = std::move(paths);
    return std::nullopt;
}

std::optional<LoadError> parse_runtime_assets(
    const Document& document,
    const Value& root,
    RuntimeAssetReferences* runtime_assets_out) {
    const Value* runtime_assets = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "runtime", Value::Type::Object, "$", &runtime_assets)) {
        return error;
    }

    std::string skeleton_path;
    if (const auto error = read_required_string(
            document, *runtime_assets, "skeleton", "$.runtime", &skeleton_path)) {
        return error;
    }
    if (skeleton_path.empty()) {
        return validation_error(
            document,
            runtime_assets->location(),
            "$.runtime.skeleton",
            "path must not be empty");
    }

    const Value* atlas_paths = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, *runtime_assets, "atlases", Value::Type::Array, "$.runtime", &atlas_paths)) {
        return error;
    }
    if (atlas_paths->as_array().empty()) {
        return validation_error(
            document,
            atlas_paths->location(),
            "$.runtime.atlases",
            "array must not be empty");
    }

    RuntimeAssetReferences runtime_assets_value;
    runtime_assets_value.skeleton_path = std::filesystem::path(skeleton_path);
    if (const auto error = parse_path_array(
            document, *atlas_paths, "$.runtime.atlases", &runtime_assets_value.atlas_paths)) {
        return error;
    }

    *runtime_assets_out = std::move(runtime_assets_value);
    return std::nullopt;
}

std::optional<LoadError> parse_snap_settings(
    const Document& document,
    const Value& root,
    std::optional<ProjectSnapSettings>* settings_out) {
    const Value* snap = find_optional_member(root, "snap");
    if (snap == nullptr) {
        settings_out->reset();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *snap, Value::Type::Object, "$.snap")) {
        return error;
    }

    ProjectSnapSettings settings;
    settings.preserved_source = *snap;
    if (const auto error = read_optional_boolean(
            document,
            *snap,
            "world_grid_enabled",
            "$.snap",
            &settings.world_grid_enabled)) {
        return error;
    }
    if (const auto error = read_optional_boolean(
            document,
            *snap,
            "local_angle_enabled",
            "$.snap",
            &settings.local_angle_enabled)) {
        return error;
    }
    if (const auto error = read_optional_boolean(
            document,
            *snap,
            "absolute_scale_enabled",
            "$.snap",
            &settings.absolute_scale_enabled)) {
        return error;
    }
    if (const auto error = read_optional_boolean(
            document,
            *snap,
            "magnetic_vertex_enabled",
            "$.snap",
            &settings.magnetic_vertex_enabled)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            *snap,
            "world_grid_step",
            "$.snap",
            &settings.world_grid_step)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            *snap,
            "local_angle_step_degrees",
            "$.snap",
            &settings.local_angle_step_degrees)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document,
            *snap,
            "absolute_scale_step",
            "$.snap",
            &settings.absolute_scale_step)) {
        return error;
    }

    const auto validate_step = [&](double step, std::string_view key)
        -> std::optional<LoadError> {
        if (std::isfinite(step) && step > 0.0) {
            return std::nullopt;
        }
        const Value* member = find_optional_member(*snap, key);
        return validation_error(
            document,
            member != nullptr ? member->location() : snap->location(),
            "$.snap." + std::string(key),
            "snap steps must be finite and greater than zero");
    };
    if (const auto error = validate_step(settings.world_grid_step, "world_grid_step")) {
        return error;
    }
    if (const auto error = validate_step(
            settings.local_angle_step_degrees, "local_angle_step_degrees")) {
        return error;
    }
    if (const auto error = validate_step(
            settings.absolute_scale_step, "absolute_scale_step")) {
        return error;
    }

    *settings_out = std::move(settings);
    return std::nullopt;
}

std::optional<LoadError> read_required_boolean(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    bool* value_out) {
    const Value* member = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, object, key, Value::Type::Boolean, json_path, &member)) {
        return error;
    }
    *value_out = member->as_boolean();
    return std::nullopt;
}

template <typename Definition, typename Parse>
std::optional<LoadError> parse_parameter_model_typed_array(
    const Document& document,
    const Value& model,
    std::string_view key,
    std::vector<Definition>* values_out,
    Parse&& parse) {
    const Value* array = find_optional_member(model, key);
    if (array == nullptr) {
        values_out->clear();
        return std::nullopt;
    }
    const std::string path = "$.parameter_model." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *array, Value::Type::Array, path)) {
        return error;
    }
    std::vector<Definition> values;
    values.reserve(array->as_array().size());
    std::vector<std::string> ids;
    for (std::size_t index = 0U; index < array->as_array().size(); ++index) {
        const Value& value = array->as_array()[index];
        const std::string entry_path = path + "[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, value, Value::Type::Object, entry_path)) {
            return error;
        }
        Definition definition;
        std::string message;
        if (!parse(value, &definition, &message)) {
            return validation_error(
                document,
                value.location(),
                entry_path,
                message.empty() ? "invalid typed parameter-model definition" : message);
        }
        if (definition.id.empty()) {
            return validation_error(
                document, value.location(), entry_path + ".id", "id must not be empty");
        }
        if (std::find(ids.begin(), ids.end(), definition.id) != ids.end()) {
            return validation_error(
                document, value.location(), entry_path + ".id", "ids must be unique");
        }
        ids.push_back(definition.id);
        values.push_back(std::move(definition));
    }
    *values_out = std::move(values);
    return std::nullopt;
}

std::optional<LoadError> parse_parameter_model(
    const Document& document,
    const Value& root,
    std::optional<ParameterModel>* model_out) {
    const Value* model_value = find_optional_member(root, "parameter_model");
    if (model_value == nullptr) {
        model_out->reset();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *model_value, Value::Type::Object, "$.parameter_model")) {
        return error;
    }

    ParameterModel model;
    model.source = *model_value;

    if (const Value* parameters = find_optional_member(*model_value, "parameters")) {
        if (const auto error = marrow::runtime::json::require_type(
                document,
                *parameters,
                Value::Type::Array,
                "$.parameter_model.parameters")) {
            return error;
        }
        std::vector<std::string> ids;
        model.parameters.reserve(parameters->as_array().size());
        for (std::size_t index = 0U; index < parameters->as_array().size(); ++index) {
            const Value& value = parameters->as_array()[index];
            const std::string path =
                "$.parameter_model.parameters[" + std::to_string(index) + "]";
            if (const auto error = marrow::runtime::json::require_type(
                    document, value, Value::Type::Object, path)) {
                return error;
            }
            ParameterAuthoringDefinition parameter;
            std::string type;
            if (const auto error = read_required_string(
                    document, value, "id", path, &parameter.id)) {
                return error;
            }
            if (const auto error = read_required_string(
                    document, value, "name", path, &parameter.name)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document, value, "min", path, &parameter.min_value)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document, value, "max", path, &parameter.max_value)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document, value, "default", path, &parameter.default_value)) {
                return error;
            }
            if (const auto error = read_required_string(
                    document, value, "type", path, &type)) {
                return error;
            }
            if (const auto error = read_required_boolean(
                    document, value, "clamp", path, &parameter.clamp)) {
                return error;
            }
            if (parameter.id.empty() || parameter.name.empty()) {
                return validation_error(
                    document,
                    value.location(),
                    path,
                    "parameter id and name must not be empty");
            }
            if (!std::isfinite(parameter.min_value) ||
                !std::isfinite(parameter.max_value) ||
                !std::isfinite(parameter.default_value) ||
                parameter.min_value > parameter.max_value ||
                (parameter.clamp &&
                 (parameter.default_value < parameter.min_value ||
                  parameter.default_value > parameter.max_value))) {
                return validation_error(
                    document,
                    value.location(),
                    path,
                    "parameter range and default must be finite and ordered");
            }
            if (type == "continuous") {
                parameter.type = ParameterAuthoringType::Continuous;
            } else if (type == "discrete") {
                parameter.type = ParameterAuthoringType::Discrete;
            } else {
                return validation_error(
                    document,
                    value.location(),
                    path + ".type",
                    "type must be continuous or discrete");
            }
            double ui_step = 0.0;
            if (const auto error = read_optional_number(
                    document, value, "ui_step", path, &ui_step)) {
                return error;
            }
            if (find_optional_member(value, "ui_step") != nullptr) {
                if (!std::isfinite(ui_step) || ui_step <= 0.0) {
                    return validation_error(
                        document,
                        value.location(),
                        path + ".ui_step",
                        "ui_step must be finite and greater than zero");
                }
                parameter.ui_step = ui_step;
            }
            std::string units;
            if (const auto error = read_optional_string(
                    document, value, "units", path, &units)) {
                return error;
            }
            if (find_optional_member(value, "units") != nullptr) {
                parameter.units = std::move(units);
            }
            if (std::find(ids.begin(), ids.end(), parameter.id) != ids.end()) {
                return validation_error(
                    document, value.location(), path + ".id", "parameter ids must be unique");
            }
            ids.push_back(parameter.id);
            model.parameters.push_back(std::move(parameter));
        }
    }

    if (const Value* groups = find_optional_member(*model_value, "groups")) {
        if (const auto error = marrow::runtime::json::require_type(
                document,
                *groups,
                Value::Type::Array,
                "$.parameter_model.groups")) {
            return error;
        }
        std::vector<std::string> ids;
        model.groups.reserve(groups->as_array().size());
        for (std::size_t index = 0U; index < groups->as_array().size(); ++index) {
            const Value& value = groups->as_array()[index];
            const std::string path =
                "$.parameter_model.groups[" + std::to_string(index) + "]";
            if (const auto error = marrow::runtime::json::require_type(
                    document, value, Value::Type::Object, path)) {
                return error;
            }
            ParameterGroupAuthoringDefinition group;
            if (const auto error = read_required_string(
                    document, value, "id", path, &group.id)) {
                return error;
            }
            if (const auto error = read_required_string(
                    document, value, "name", path, &group.name)) {
                return error;
            }
            const Value* parameter_ids = nullptr;
            if (const auto error = marrow::runtime::json::require_member(
                    document,
                    value,
                    "parameters",
                    Value::Type::Array,
                    path,
                    &parameter_ids)) {
                return error;
            }
            if (const auto error = parse_string_array(
                    document,
                    *parameter_ids,
                    path + ".parameters",
                    &group.parameter_ids)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document, value, "collapsed", path, &group.collapsed)) {
                return error;
            }
            std::string color_tag;
            if (const auto error = read_optional_string(
                    document, value, "color_tag", path, &color_tag)) {
                return error;
            }
            if (find_optional_member(value, "color_tag") != nullptr) {
                group.color_tag = std::move(color_tag);
            }
            std::string exclusive_mode;
            if (const auto error = read_optional_string(
                    document, value, "exclusive_mode", path, &exclusive_mode)) {
                return error;
            }
            if (find_optional_member(value, "exclusive_mode") != nullptr) {
                group.exclusive_mode = std::move(exclusive_mode);
            }
            if (group.id.empty() || group.name.empty()) {
                return validation_error(
                    document,
                    value.location(),
                    path,
                    "group id and name must not be empty");
            }
            if (std::find(ids.begin(), ids.end(), group.id) != ids.end()) {
                return validation_error(
                    document, value.location(), path + ".id", "group ids must be unique");
            }
            std::vector<std::string> sorted_ids = group.parameter_ids;
            std::sort(sorted_ids.begin(), sorted_ids.end());
            if (std::adjacent_find(sorted_ids.begin(), sorted_ids.end()) != sorted_ids.end()) {
                return validation_error(
                    document,
                    value.location(),
                    path + ".parameters",
                    "group parameter ids must be unique");
            }
            for (const std::string& parameter_id : group.parameter_ids) {
                if (model.find_parameter(parameter_id) == nullptr) {
                    return validation_error(
                        document,
                        value.location(),
                        path + ".parameters",
                        "group references an unknown parameter id");
                }
            }
            ids.push_back(group.id);
            model.groups.push_back(std::move(group));
        }
    }

    if (const auto error = parse_parameter_model_typed_array(
            document,
            *model_value,
            "deformers",
            &model.deformers,
            parse_parameter_deformer_authoring_value)) {
        return error;
    }
    if (const auto error = parse_parameter_model_typed_array(
            document,
            *model_value,
            "blend_shapes",
            &model.blend_shapes,
            parse_parameter_shape_authoring_value)) {
        return error;
    }
    if (const auto error = parse_parameter_model_typed_array(
            document,
            *model_value,
            "art_paths",
            &model.art_paths,
            parse_art_path_authoring_value)) {
        return error;
    }
    if (const auto error = parse_parameter_model_typed_array(
            document,
            *model_value,
            "expressions",
            &model.expressions,
            parse_expression_authoring_value)) {
        return error;
    }
    if (const Value* lip_sync = find_optional_member(*model_value, "lip_sync")) {
        if (const auto error = marrow::runtime::json::require_type(
                document,
                *lip_sync,
                Value::Type::Object,
                "$.parameter_model.lip_sync")) {
            return error;
        }
        std::string message;
        if (!parse_lip_sync_authoring_value(*lip_sync, &model.lip_sync, &message)) {
            return validation_error(
                document,
                lip_sync->location(),
                "$.parameter_model.lip_sync",
                message.empty() ? "invalid typed lip-sync definition" : message);
        }
    }

    *model_out = model.empty()
        ? std::optional<ParameterModel>{}
        : std::optional<ParameterModel>{std::move(model)};
    return std::nullopt;
}

std::optional<LoadError> parse_animation_edits(
    const Document& document,
    const Value& root,
    std::vector<AnimationEdit>* edits_out) {
    const Value* edits_value = find_optional_member(root, "animation_edits");
    if (edits_value == nullptr) {
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *edits_value, Value::Type::Array, "$.animation_edits")) {
        return error;
    }

    std::vector<AnimationEdit> edits;
    edits.reserve(edits_value->as_array().size());
    for (std::size_t index = 0; index < edits_value->as_array().size(); ++index) {
        const Value& edit_value = edits_value->as_array()[index];
        const std::string edit_path = "$.animation_edits[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, edit_value, Value::Type::Object, edit_path)) {
            return error;
        }

        std::string operation;
        if (const auto error = read_required_string(
                document, edit_value, "op", edit_path, &operation)) {
            return error;
        }
        if (operation.empty()) {
            return validation_error(
                document,
                edit_value.location(),
                edit_path + ".op",
                "operation must not be empty");
        }

        AnimationEdit edit;
        edit.preserved_source = edit_value;
        if (operation == "create") {
            edit.kind = AnimationEditKind::Create;
            if (const auto error = read_required_string(
                    document, edit_value, "name", edit_path, &edit.name)) {
                return error;
            }
            const Value* animation_value = nullptr;
            if (const auto error = marrow::runtime::json::require_member(
                    document,
                    edit_value,
                    "animation",
                    Value::Type::Object,
                    edit_path,
                    &animation_value)) {
                return error;
            }
            edit.animation = *animation_value;
        } else if (operation == "rename") {
            edit.kind = AnimationEditKind::Rename;
            if (const auto error = read_required_string(
                    document, edit_value, "from", edit_path, &edit.name)) {
                return error;
            }
            if (const auto error = read_required_string(
                    document, edit_value, "to", edit_path, &edit.new_name)) {
                return error;
            }
        } else if (operation == "delete") {
            edit.kind = AnimationEditKind::Delete;
            if (const auto error = read_required_string(
                    document, edit_value, "name", edit_path, &edit.name)) {
                return error;
            }
        } else if (operation == "set_duration") {
            edit.kind = AnimationEditKind::SetDuration;
            if (const auto error = read_required_string(
                    document, edit_value, "name", edit_path, &edit.name)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document, edit_value, "duration", edit_path, &edit.duration)) {
                return error;
            }
            if (!std::isfinite(edit.duration) || edit.duration < 0.0 ||
                edit.duration > static_cast<double>(
                    std::numeric_limits<runtime::AnimationScalar>::max())) {
                return validation_error(
                    document,
                    edit_value.location(),
                    edit_path + ".duration",
                    "duration must be finite, non-negative, and within the runtime float32 range");
            }
        } else {
            edit.kind = AnimationEditKind::Unknown;
        }

        if (edit.kind != AnimationEditKind::Unknown &&
            (edit.name.empty() ||
             (edit.kind == AnimationEditKind::Rename && edit.new_name.empty()))) {
            return validation_error(
                document,
                edit_value.location(),
                edit_path,
                "animation names must not be empty");
        }
        if (edit.kind == AnimationEditKind::Rename && edit.name == edit.new_name) {
            return validation_error(
                document,
                edit_value.location(),
                edit_path,
                "rename source and destination must differ");
        }
        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

/**
 * @brief Parses `$.editor.import_sources`, MAR-188's typed PSD provenance.
 *
 * Shape copied from `editor.viewport.onion_skin` above, the exact structural
 * analogue: an optional object two levels under `$.editor`.
 *
 * `layers` ABSENT is legal and means an empty vector -- a provenance record that
 * names a source but has no mapping yet is meaningful (a first import that has
 * not been committed). `layers` EMPTY is likewise legal.
 *
 * Duplicate identities are refused HERE, at load, not at planning time: a project
 * whose provenance cannot key itself is malformed, and the file-format layer is
 * where this tree already refuses that class (`ids must be unique`,
 * `atlas pack output paths must be unique`).
 */
std::optional<LoadError> parse_import_sources(
    const Document& document,
    const Value& editor,
    std::optional<ProjectImportSources>* import_sources_out) {
    const Value* import_sources = find_optional_member(editor, "import_sources");
    if (import_sources == nullptr) {
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *import_sources, Value::Type::Object, "$.editor.import_sources")) {
        return error;
    }

    ProjectImportSources sources;
    const Value* psd = find_optional_member(*import_sources, "psd");
    if (psd != nullptr) {
        if (const auto error = marrow::runtime::json::require_type(
                document, *psd, Value::Type::Object, "$.editor.import_sources.psd")) {
            return error;
        }

        PsdImportProvenance provenance;
        std::string source_path;
        if (const auto error = read_required_string(
                document, *psd, "path", "$.editor.import_sources.psd", &source_path)) {
            return error;
        }
        if (source_path.empty()) {
            return validation_error(
                document,
                psd->location(),
                "$.editor.import_sources.psd.path",
                "psd source paths must not be empty");
        }
        provenance.source_path = source_path;

        std::string layers_directory;
        if (const auto error = read_optional_string(
                document,
                *psd,
                "layers_directory",
                "$.editor.import_sources.psd",
                &layers_directory)) {
            return error;
        }
        if (layers_directory.empty()) {
            return validation_error(
                document,
                psd->location(),
                "$.editor.import_sources.psd.layers_directory",
                "psd layer directories must not be empty");
        }
        provenance.layers_directory = layers_directory;

        if (const Value* layers = find_optional_member(*psd, "layers")) {
            if (const auto error = marrow::runtime::json::require_type(
                    document, *layers, Value::Type::Array,
                    "$.editor.import_sources.psd.layers")) {
                return error;
            }

            // Identities are refused by exact match on (group_path, layer_name).
            // Nothing here infers or normalises: a name is what the PSD said.
            std::set<std::string> seen_identities;
            provenance.layers.reserve(layers->as_array().size());
            for (std::size_t index = 0; index < layers->as_array().size(); ++index) {
                const Value& layer_value = layers->as_array()[index];
                const std::string layer_path =
                    "$.editor.import_sources.psd.layers[" + std::to_string(index) + "]";
                if (const auto error = marrow::runtime::json::require_type(
                        document, layer_value, Value::Type::Object, layer_path)) {
                    return error;
                }

                PsdLayerProvenance layer;
                if (const Value* group_path =
                        find_optional_member(layer_value, "group_path")) {
                    if (const auto error = marrow::runtime::json::require_type(
                            document, *group_path, Value::Type::Array,
                            layer_path + ".group_path")) {
                        return error;
                    }
                    layer.group_path.reserve(group_path->as_array().size());
                    for (std::size_t segment_index = 0;
                         segment_index < group_path->as_array().size();
                         ++segment_index) {
                        const Value& segment = group_path->as_array()[segment_index];
                        const std::string segment_path = layer_path + ".group_path[" +
                            std::to_string(segment_index) + "]";
                        if (const auto error = marrow::runtime::json::require_type(
                                document, segment, Value::Type::String, segment_path)) {
                            return error;
                        }
                        layer.group_path.push_back(segment.as_string());
                    }
                }

                if (const auto error = read_required_string(
                        document, layer_value, "layer", layer_path, &layer.layer_name)) {
                    return error;
                }
                if (layer.layer_name.empty()) {
                    return validation_error(
                        document,
                        layer_value.location(),
                        layer_path + ".layer",
                        "psd layer names must not be empty");
                }

                if (const auto error = read_optional_string(
                        document, layer_value, "slot", layer_path, &layer.slot_name)) {
                    return error;
                }
                if (const auto error = read_optional_string(
                        document, layer_value, "attachment", layer_path,
                        &layer.attachment_name)) {
                    return error;
                }
                if (const auto error = read_optional_string(
                        document, layer_value, "bone", layer_path, &layer.bone_name)) {
                    return error;
                }

                if (const auto error = read_required_string(
                        document, layer_value, "image", layer_path, &layer.image_file)) {
                    return error;
                }
                if (layer.image_file.empty()) {
                    return validation_error(
                        document,
                        layer_value.location(),
                        layer_path + ".image",
                        "psd layer image names must not be empty");
                }
                // The bare-file-name invariant, ENFORCED rather than trusted. A
                // stored image is always a direct child of `layers_directory`, so
                // anything carrying a separator or a `..` would name a file the
                // rebase family cannot reach.
                if (std::filesystem::path(layer.image_file).filename().generic_string() !=
                    layer.image_file) {
                    return validation_error(
                        document,
                        layer_value.location(),
                        layer_path + ".image",
                        "psd layer image names must not contain a directory separator");
                }

                std::string identity;
                for (const std::string& segment : layer.group_path) {
                    identity += segment;
                    identity += '\x1f';
                }
                identity += layer.layer_name;
                if (!seen_identities.insert(identity).second) {
                    return validation_error(
                        document,
                        layer_value.location(),
                        layer_path,
                        "psd layer identities must be unique");
                }

                provenance.layers.push_back(std::move(layer));
            }
        }

        sources.psd = std::move(provenance);
    }

    *import_sources_out = std::move(sources);
    return std::nullopt;
}

std::optional<LoadError> parse_editor_metadata(
    const Document& document,
    const Value& root,
    ProjectMetadata* metadata_out) {
    const Value* editor = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "editor", Value::Type::Object, "$", &editor)) {
        return error;
    }

    ProjectMetadata metadata;
    if (const auto error = read_required_string(
            document, *editor, "name", "$.editor", &metadata.name)) {
        return error;
    }
    if (metadata.name.empty()) {
        return validation_error(
            document,
            editor->location(),
            "$.editor.name",
            "name must not be empty");
    }

    if (const auto error = read_optional_string(
            document, *editor, "active_animation", "$.editor", &metadata.active_animation)) {
        return error;
    }
    std::string export_directory;
    if (const auto error = read_optional_string(
            document, *editor, "export_directory", "$.editor", &export_directory)) {
        return error;
    }
    if (!export_directory.empty()) {
        metadata.export_directory = std::filesystem::path(export_directory);
    }
    if (const auto error = read_optional_string(
            document, *editor, "notes", "$.editor", &metadata.notes)) {
        return error;
    }

    if (const Value* preview_skins = find_optional_member(*editor, "preview_skins")) {
        if (const auto error = parse_string_array(
                document, *preview_skins, "$.editor.preview_skins", &metadata.preview_skins)) {
            return error;
        }
    }

    if (const Value* timeline = find_optional_member(*editor, "timeline")) {
        if (const auto error = marrow::runtime::json::require_type(
                document, *timeline, Value::Type::Object, "$.editor.timeline")) {
            return error;
        }
        if (const auto error = read_optional_number(
                document,
                *timeline,
                "fps",
                "$.editor.timeline",
                &metadata.timeline.frames_per_second)) {
            return error;
        }
        if (!std::isfinite(metadata.timeline.frames_per_second) ||
            metadata.timeline.frames_per_second <= 0.0) {
            return validation_error(
                document,
                timeline->location(),
                "$.editor.timeline.fps",
                "timeline fps must be finite and greater than zero");
        }
    }

    if (const Value* viewport = find_optional_member(*editor, "viewport")) {
        if (const auto error = marrow::runtime::json::require_type(
                document, *viewport, Value::Type::Object, "$.editor.viewport")) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, *viewport, "pan_x", "$.editor.viewport", &metadata.viewport.pan_x)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, *viewport, "pan_y", "$.editor.viewport", &metadata.viewport.pan_y)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, *viewport, "zoom", "$.editor.viewport", &metadata.viewport.zoom)) {
            return error;
        }
        if (metadata.viewport.zoom <= 0.0) {
            return validation_error(
                document,
                viewport->location(),
                "$.editor.viewport.zoom",
                "zoom must be greater than zero");
        }

        if (const Value* onion_skin = find_optional_member(*viewport, "onion_skin")) {
            if (const auto error = marrow::runtime::json::require_type(
                    document,
                    *onion_skin,
                    Value::Type::Object,
                    "$.editor.viewport.onion_skin")) {
                return error;
            }

            std::string mode_name;
            if (const auto error = read_optional_string(
                    document,
                    *onion_skin,
                    "mode",
                    "$.editor.viewport.onion_skin",
                    &mode_name)) {
                return error;
            }
            if (!mode_name.empty()) {
                const auto mode = onion_skin_mode_from_key(mode_name);
                if (!mode.has_value()) {
                    return validation_error(
                        document,
                        onion_skin->location(),
                        "$.editor.viewport.onion_skin.mode",
                        "mode must be 'frame' or 'keyframe'");
                }
                metadata.viewport.onion_skin.mode = *mode;
            }

            if (const auto error = read_optional_boolean(
                    document,
                    *onion_skin,
                    "enabled",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.enabled)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *onion_skin,
                    "anchor",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.anchor_to_zero)) {
                return error;
            }
            if (const auto error = read_optional_integer(
                    document,
                    *onion_skin,
                    "before",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.before_count)) {
                return error;
            }
            if (const auto error = read_optional_integer(
                    document,
                    *onion_skin,
                    "after",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.after_count)) {
                return error;
            }
            if (const auto error = read_optional_integer(
                    document,
                    *onion_skin,
                    "step",
                    "$.editor.viewport.onion_skin",
                    &metadata.viewport.onion_skin.step)) {
                return error;
            }

            if (metadata.viewport.onion_skin.before_count < 0 ||
                metadata.viewport.onion_skin.before_count > 6) {
                return validation_error(
                    document,
                    onion_skin->location(),
                    "$.editor.viewport.onion_skin.before",
                    "before must stay within [0, 6]");
            }
            if (metadata.viewport.onion_skin.after_count < 0 ||
                metadata.viewport.onion_skin.after_count > 6) {
                return validation_error(
                    document,
                    onion_skin->location(),
                    "$.editor.viewport.onion_skin.after",
                    "after must stay within [0, 6]");
            }
            if (metadata.viewport.onion_skin.step <= 0) {
                return validation_error(
                    document,
                    onion_skin->location(),
                    "$.editor.viewport.onion_skin.step",
                    "step must be greater than zero");
            }
        }

        if (const Value* debug_overlay = find_optional_member(*viewport, "debug_overlay")) {
            if (const auto error = marrow::runtime::json::require_type(
                    document,
                    *debug_overlay,
                    Value::Type::Object,
                    "$.editor.viewport.debug_overlay")) {
                return error;
            }

            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "bones",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.bones)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "ik",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.ik_constraints)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "path",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.path_constraints)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "physics",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.physics_constraints)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "meshes",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.mesh_wireframes)) {
                return error;
            }
            if (const auto error = read_optional_boolean(
                    document,
                    *debug_overlay,
                    "bounds",
                    "$.editor.viewport.debug_overlay",
                    &metadata.viewport.debug_overlay.bounding_boxes)) {
                return error;
            }
        }
    }

    if (const auto error =
            parse_import_sources(document, *editor, &metadata.import_sources)) {
        return error;
    }

    *metadata_out = std::move(metadata);
    return std::nullopt;
}

std::optional<LoadError> parse_atlas_pack_sprites(
    const Document& document,
    const Value& sprites_value,
    std::string_view json_path,
    std::vector<AtlasPackSprite>* sprites_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, sprites_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (sprites_value.as_array().empty()) {
        return validation_error(
            document,
            sprites_value.location(),
            std::string(json_path),
            "atlas pack sprite arrays must not be empty");
    }

    std::vector<AtlasPackSprite> sprites;
    sprites.reserve(sprites_value.as_array().size());
    for (std::size_t index = 0; index < sprites_value.as_array().size(); ++index) {
        const Value& sprite_value = sprites_value.as_array()[index];
        const std::string sprite_path =
            std::string(json_path) + "[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, sprite_value, Value::Type::Object, sprite_path)) {
            return error;
        }

        AtlasPackSprite sprite;
        if (const auto error = read_required_string(
                document, sprite_value, "name", sprite_path, &sprite.region_name)) {
            return error;
        }
        if (sprite.region_name.empty()) {
            return validation_error(
                document,
                sprite_value.location(),
                sprite_path + ".name",
                "atlas pack sprite names must not be empty");
        }

        std::string image_path;
        if (const auto error = read_required_string(
                document, sprite_value, "image", sprite_path, &image_path)) {
            return error;
        }
        if (image_path.empty()) {
            return validation_error(
                document,
                sprite_value.location(),
                sprite_path + ".image",
                "atlas pack sprite image paths must not be empty");
        }
        sprite.image_path = std::filesystem::path(image_path);

        double origin_x = 0.0;
        if (const auto error = read_optional_number(
                document, sprite_value, "origin_x", sprite_path, &origin_x)) {
            return error;
        }
        if (find_optional_member(sprite_value, "origin_x") != nullptr) {
            sprite.origin_x = origin_x;
        }

        double origin_y = 0.0;
        if (const auto error = read_optional_number(
                document, sprite_value, "origin_y", sprite_path, &origin_y)) {
            return error;
        }
        if (find_optional_member(sprite_value, "origin_y") != nullptr) {
            sprite.origin_y = origin_y;
        }

        const auto duplicate = std::find_if(
            sprites.begin(),
            sprites.end(),
            [&](const AtlasPackSprite& existing) {
                return existing.region_name == sprite.region_name;
            });
        if (duplicate != sprites.end()) {
            return validation_error(
                document,
                sprite_value.location(),
                sprite_path + ".name",
                "atlas pack sprite names must be unique");
        }

        sprites.push_back(std::move(sprite));
    }

    *sprites_out = std::move(sprites);
    return std::nullopt;
}

std::optional<LoadError> parse_atlas_pack_definitions(
    const Document& document,
    const Value& root,
    std::vector<AtlasPackDefinition>* atlas_pack_definitions_out) {
    const Value* atlas_packs = find_optional_member(root, "atlas_packs");
    if (atlas_packs == nullptr) {
        atlas_pack_definitions_out->clear();
        return std::nullopt;
    }

    if (const auto error = marrow::runtime::json::require_type(
            document, *atlas_packs, Value::Type::Array, "$.atlas_packs")) {
        return error;
    }

    std::vector<AtlasPackDefinition> atlas_pack_definitions;
    atlas_pack_definitions.reserve(atlas_packs->as_array().size());
    for (std::size_t index = 0; index < atlas_packs->as_array().size(); ++index) {
        const Value& definition_value = atlas_packs->as_array()[index];
        const std::string definition_path = "$.atlas_packs[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, definition_value, Value::Type::Object, definition_path)) {
            return error;
        }

        AtlasPackDefinition definition;
        std::string atlas_path;
        if (const auto error = read_required_string(
                document, definition_value, "atlas", definition_path, &atlas_path)) {
            return error;
        }
        if (atlas_path.empty()) {
            return validation_error(
                document,
                definition_value.location(),
                definition_path + ".atlas",
                "atlas pack output paths must not be empty");
        }
        definition.atlas_path = std::filesystem::path(atlas_path);

        if (const auto error = read_optional_string(
                document,
                definition_value,
                "atlas_name",
                definition_path,
                &definition.atlas_name)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document,
                definition_value,
                "filter_min",
                definition_path,
                &definition.filter_min)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document,
                definition_value,
                "filter_mag",
                definition_path,
                &definition.filter_mag)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document, definition_value, "wrap_x", definition_path, &definition.wrap_x)) {
            return error;
        }
        if (const auto error = read_optional_string(
                document, definition_value, "wrap_y", definition_path, &definition.wrap_y)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document,
                definition_value,
                "premultiplied_alpha",
                definition_path,
                &definition.premultiplied_alpha)) {
            return error;
        }
        if (const auto error = read_optional_integer(
                document, definition_value, "padding", definition_path, &definition.padding)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document, definition_value, "trim", definition_path, &definition.trim)) {
            return error;
        }
        if (const auto error = read_optional_integer(
                document, definition_value, "bleed", definition_path, &definition.bleed)) {
            return error;
        }

        const Value* sprites_value = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                definition_value,
                "sprites",
                Value::Type::Array,
                definition_path,
                &sprites_value)) {
            return error;
        }
        if (const auto error = parse_atlas_pack_sprites(
                document,
                *sprites_value,
                definition_path + ".sprites",
                &definition.sprites)) {
            return error;
        }

        const auto duplicate = std::find_if(
            atlas_pack_definitions.begin(),
            atlas_pack_definitions.end(),
            [&](const AtlasPackDefinition& existing) {
                return existing.atlas_path == definition.atlas_path;
            });
        if (duplicate != atlas_pack_definitions.end()) {
            return validation_error(
                document,
                definition_value.location(),
                definition_path + ".atlas",
                "atlas pack output paths must be unique");
        }

        atlas_pack_definitions.push_back(std::move(definition));
    }

    *atlas_pack_definitions_out = std::move(atlas_pack_definitions);
    return std::nullopt;
}

std::optional<LoadError> parse_optional_xy_vector(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view json_path,
    runtime::AttachmentVertex* value_out) {
    const Value* member = find_optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }

    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (const auto error = marrow::runtime::json::require_type(
            document, *member, Value::Type::Object, member_path)) {
        return error;
    }

    if (const auto error = read_optional_number(
            document, *member, "x", member_path, &value_out->x)) {
        return error;
    }
    if (const auto error = read_optional_number(
            document, *member, "y", member_path, &value_out->y)) {
        return error;
    }

    return std::nullopt;
}

std::optional<LoadError> parse_interpolation(
    const Document& document,
    const Value& keyframe_value,
    std::string_view keyframe_path,
    runtime::Interpolation* interpolation_out) {
    *interpolation_out = runtime::Interpolation::linear();

    const Value* curve_value = find_optional_member(keyframe_value, "curve");
    if (curve_value == nullptr) {
        return std::nullopt;
    }

    const std::string curve_path = std::string(keyframe_path) + ".curve";
    if (curve_value->is_string()) {
        const std::string& curve_name = curve_value->as_string();
        if (curve_name == "linear") {
            *interpolation_out = runtime::Interpolation::linear();
            return std::nullopt;
        }
        if (curve_name == "stepped") {
            *interpolation_out = runtime::Interpolation::stepped();
            return std::nullopt;
        }

        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "curve must be 'linear', 'stepped', or a 4-number bezier array");
    }

    if (!curve_value->is_array()) {
        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "curve must be 'linear', 'stepped', or a 4-number bezier array");
    }

    const Value::Array& control_points = curve_value->as_array();
    if (control_points.size() != 4) {
        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "bezier curve arrays must contain exactly 4 control point numbers");
    }

    double coordinates[4] = {};
    for (std::size_t index = 0; index < control_points.size(); ++index) {
        if (const auto error = marrow::runtime::json::require_type(
                document,
                control_points[index],
                Value::Type::Number,
                curve_path + "[" + std::to_string(index) + "]")) {
            return error;
        }
        coordinates[index] = control_points[index].as_number();
    }

    if (coordinates[0] < 0.0 || coordinates[0] > 1.0 ||
        coordinates[2] < 0.0 || coordinates[2] > 1.0) {
        return validation_error(
            document,
            curve_value->location(),
            curve_path,
            "bezier x control points must stay within [0, 1]");
    }

    *interpolation_out = runtime::Interpolation::cubic_bezier(
        coordinates[0], coordinates[1], coordinates[2], coordinates[3]);
    return std::nullopt;
}

/**
 * @brief Parses the optional MAR-171 `curve_mode` / `curve_driver` pair.
 *
 * Both members are absent from every pre-MAR-171 document, and absence means
 * `Manual` with the family's default driver. A driver on a manual key is a load
 * error rather than silently dropped or silently kept: dropping would lose
 * authored data on the next save and keeping would create an in-memory state
 * that never round trips. `channel` is read only for Transform keys.
 */
std::optional<LoadError> parse_curve_intent(
    const Document& document,
    const Value& keyframe_value,
    std::string_view keyframe_path,
    TimelineKeyKind kind,
    TransformTimelineChannel channel,
    TimelineCurveMode* mode_out,
    TimelineScalarComponent* driver_out) {
    *mode_out = TimelineCurveMode::Manual;
    *driver_out = default_curve_driver(kind, channel);

    const Value* mode_value = find_optional_member(keyframe_value, "curve_mode");
    if (mode_value != nullptr) {
        const std::string mode_path = std::string(keyframe_path) + ".curve_mode";
        const auto parsed = mode_value->is_string()
            ? curve_mode_from_token(mode_value->as_string())
            : std::nullopt;
        if (!parsed.has_value()) {
            return validation_error(
                document,
                mode_value->location(),
                mode_path,
                "curve_mode must be 'manual' or 'auto'");
        }
        *mode_out = *parsed;
    }

    const Value* driver_value = find_optional_member(keyframe_value, "curve_driver");
    if (driver_value == nullptr) {
        return std::nullopt;
    }
    const std::string driver_path = std::string(keyframe_path) + ".curve_driver";
    if (*mode_out != TimelineCurveMode::Auto) {
        return validation_error(
            document,
            driver_value->location(),
            driver_path,
            "curve_driver requires curve_mode 'auto'");
    }
    const auto parsed_driver = driver_value->is_string()
        ? curve_driver_from_token(driver_value->as_string())
        : std::nullopt;
    if (!parsed_driver.has_value()) {
        return validation_error(
            document,
            driver_value->location(),
            driver_path,
            "curve_driver must be one of angle, x, y, r, g, b, a");
    }
    if (!curve_driver_is_authorable(kind, channel, *parsed_driver)) {
        return validation_error(
            document,
            driver_value->location(),
            driver_path,
            "curve_driver must name a component this timeline owns");
    }
    *driver_out = *parsed_driver;
    return std::nullopt;
}

/** @brief Emits the MAR-171 pair only for an automatic key, so manual stays absent. */
std::optional<LoadError> parse_transform_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    TransformTimelineChannel channel,
    std::vector<TransformKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "timeline edits must contain at least one keyframe");
    }

    std::vector<TransformKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        TransformKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }
        if (is_vector_channel(channel)) {
            if (const auto error = read_required_number(
                    document, keyframe_value, "x", keyframe_path, &keyframe.x)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document, keyframe_value, "y", keyframe_path, &keyframe.y)) {
                return error;
            }
        } else {
            if (const auto error = read_required_number(
                    document, keyframe_value, "angle", keyframe_path, &keyframe.angle)) {
                return error;
            }
        }
        if (const auto error = parse_interpolation(
                document, keyframe_value, keyframe_path, &keyframe.interpolation)) {
            return error;
        }
        if (const auto error = parse_curve_intent(
                document,
                keyframe_value,
                keyframe_path,
                TimelineKeyKind::Transform,
                channel,
                &keyframe.curve_mode,
                &keyframe.curve_driver)) {
            return error;
        }
        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "timeline edit keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_transform_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<TransformTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<TransformTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path =
            "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* bones = find_optional_member(animation_value, "bones");
        if (bones == nullptr) {
            continue;
        }
        if (const auto error = marrow::runtime::json::require_type(
                document, *bones, Value::Type::Object, animation_path + ".bones")) {
            return error;
        }

        for (const auto& [bone_name, bone_value] : bones->as_object()) {
            const std::string bone_path =
                animation_path + ".bones." + bone_name;
            if (const auto error = marrow::runtime::json::require_type(
                    document, bone_value, Value::Type::Object, bone_path)) {
                return error;
            }

            for (const auto& [channel_name, timeline_value] : bone_value.as_object()) {
                const auto channel = transform_channel_from_key(channel_name);
                if (!channel.has_value()) {
                    continue;
                }

                TransformTimelineEdit edit;
                edit.animation_name = animation_name;
                edit.bone_name = bone_name;
                edit.channel = *channel;
                if (const auto error = parse_transform_keyframes(
                        document,
                        timeline_value,
                        bone_path + "." + channel_name,
                        *channel,
                        &edit.keyframes)) {
                    return error;
                }
                edits.push_back(std::move(edit));
            }
        }
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_deform_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<DeformKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "deform timeline edits must contain at least one keyframe");
    }

    std::vector<DeformKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        DeformKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }

        const Value* vertices_value = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                keyframe_value,
                "vertices",
                Value::Type::Array,
                keyframe_path,
                &vertices_value)) {
            return error;
        }
        if (vertices_value->as_array().empty() ||
            (vertices_value->as_array().size() % 2U) != 0U) {
            return validation_error(
                document,
                vertices_value->location(),
                keyframe_path + ".vertices",
                "deform keyframes must provide one x/y offset pair per vertex");
        }

        keyframe.vertex_offsets.reserve(vertices_value->as_array().size());
        for (std::size_t component_index = 0;
             component_index < vertices_value->as_array().size();
             ++component_index) {
            const Value& component_value = vertices_value->as_array()[component_index];
            const std::string component_path =
                keyframe_path + ".vertices[" + std::to_string(component_index) + "]";
            if (const auto error = marrow::runtime::json::require_type(
                    document, component_value, Value::Type::Number, component_path)) {
                return error;
            }
            keyframe.vertex_offsets.push_back(component_value.as_number());
        }

        if (const auto error = parse_interpolation(
                document, keyframe_value, keyframe_path, &keyframe.interpolation)) {
            return error;
        }
        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "deform timeline edit keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_mesh_deform_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<MeshDeformTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<MeshDeformTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path =
            "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* deform = find_optional_member(animation_value, "deform");
        if (deform == nullptr) {
            continue;
        }
        if (const auto error = marrow::runtime::json::require_type(
                document, *deform, Value::Type::Object, animation_path + ".deform")) {
            return error;
        }

        for (const auto& [slot_name, slot_value] : deform->as_object()) {
            const std::string slot_path = animation_path + ".deform." + slot_name;
            if (const auto error = marrow::runtime::json::require_type(
                    document, slot_value, Value::Type::Object, slot_path)) {
                return error;
            }

            for (const auto& [attachment_name, timeline_value] : slot_value.as_object()) {
                MeshDeformTimelineEdit edit;
                edit.animation_name = animation_name;
                edit.slot_name = slot_name;
                edit.attachment_name = attachment_name;
                if (const auto error = parse_deform_keyframes(
                        document,
                        timeline_value,
                        slot_path + "." + attachment_name,
                        &edit.keyframes)) {
                    return error;
                }
                edits.push_back(std::move(edit));
            }
        }
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_mesh_weight_vertices(
    const Document& document,
    const Value& weights_value,
    std::string_view json_path,
    std::vector<MeshWeightVertexEdit>* vertices_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, weights_value, Value::Type::Array, json_path)) {
        return error;
    }

    if (weights_value.as_array().empty()) {
        return validation_error(
            document,
            weights_value.location(),
            std::string(json_path),
            "mesh weight edits must contain one vertex influence list per vertex");
    }

    std::vector<MeshWeightVertexEdit> vertices;
    vertices.reserve(weights_value.as_array().size());

    for (std::size_t vertex_index = 0;
         vertex_index < weights_value.as_array().size();
         ++vertex_index) {
        const Value& vertex_value = weights_value.as_array()[vertex_index];
        const std::string vertex_path =
            std::string(json_path) + "[" + std::to_string(vertex_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, vertex_value, Value::Type::Array, vertex_path)) {
            return error;
        }
        if (vertex_value.as_array().empty()) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh weight edit vertices must preserve at least one bone influence");
        }
        if (vertex_value.as_array().size() >
            mesh_weight_model::kMaxMeshWeightInfluences) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh weight edit vertices support at most 4 bone influences");
        }

        MeshWeightVertexEdit vertex;
        vertex.influences.reserve(vertex_value.as_array().size());
        std::vector<std::string> seen_bones;
        double total_weight = 0.0;

        for (std::size_t influence_index = 0;
             influence_index < vertex_value.as_array().size();
             ++influence_index) {
            const Value& influence_value = vertex_value.as_array()[influence_index];
            const std::string influence_path =
                vertex_path + "[" + std::to_string(influence_index) + "]";
            if (const auto error = marrow::runtime::json::require_type(
                    document, influence_value, Value::Type::Object, influence_path)) {
                return error;
            }

            MeshWeightInfluenceEdit influence;
            if (const auto error = read_required_string(
                    document,
                    influence_value,
                    "bone",
                    influence_path,
                    &influence.bone_name)) {
                return error;
            }
            if (influence.bone_name.empty()) {
                return validation_error(
                    document,
                    influence_value.location(),
                    influence_path + ".bone",
                    "mesh weight edit bone names must not be empty");
            }
            if (std::find(seen_bones.begin(), seen_bones.end(), influence.bone_name) !=
                seen_bones.end()) {
                return validation_error(
                    document,
                    influence_value.location(),
                    influence_path + ".bone",
                    "mesh weight edit vertices must not repeat the same bone");
            }
            if (const auto error = read_required_number(
                    document,
                    influence_value,
                    "x",
                    influence_path,
                    &influence.x)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document,
                    influence_value,
                    "y",
                    influence_path,
                    &influence.y)) {
                return error;
            }
            if (const auto error = read_required_number(
                    document,
                    influence_value,
                    "weight",
                    influence_path,
                    &influence.weight)) {
                return error;
            }
            if (influence.weight <= 0.0) {
                return validation_error(
                    document,
                    influence_value.location(),
                    influence_path + ".weight",
                    "mesh weight edit bone weights must be positive");
            }

            total_weight += influence.weight;
            seen_bones.push_back(influence.bone_name);
            vertex.influences.push_back(std::move(influence));
        }

        if (total_weight <= 0.0) {
            return validation_error(
                document,
                vertex_value.location(),
                vertex_path,
                "mesh weight edit vertices must sum to a positive weight");
        }

        vertices.push_back(std::move(vertex));
    }

    *vertices_out = std::move(vertices);
    return std::nullopt;
}

std::optional<LoadError> parse_mesh_weight_attachment_edits(
    const Document& document,
    const Value& root,
    std::vector<MeshWeightAttachmentEdit>* edits_out) {
    const Value* mesh_edits = find_optional_member(root, "mesh_edits");
    if (mesh_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *mesh_edits, Value::Type::Object, "$.mesh_edits")) {
        return error;
    }

    const Value* weights_value = find_optional_member(*mesh_edits, "weights");
    if (weights_value == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *weights_value, Value::Type::Array, "$.mesh_edits.weights")) {
        return error;
    }

    std::vector<MeshWeightAttachmentEdit> edits;
    edits.reserve(weights_value->as_array().size());

    for (std::size_t edit_index = 0; edit_index < weights_value->as_array().size(); ++edit_index) {
        const Value& edit_value = weights_value->as_array()[edit_index];
        const std::string edit_path =
            "$.mesh_edits.weights[" + std::to_string(edit_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, edit_value, Value::Type::Object, edit_path)) {
            return error;
        }

        MeshWeightAttachmentEdit edit;
        if (const auto error = read_required_string(
                document, edit_value, "skin", edit_path, &edit.skin_name)) {
            return error;
        }
        if (const auto error = read_required_string(
                document, edit_value, "slot", edit_path, &edit.slot_name)) {
            return error;
        }
        if (const auto error = read_required_string(
                document, edit_value, "attachment", edit_path, &edit.attachment_name)) {
            return error;
        }
        if (edit.skin_name.empty() || edit.slot_name.empty() || edit.attachment_name.empty()) {
            return validation_error(
                document,
                edit_value.location(),
                edit_path,
                "mesh weight edits require non-empty skin, slot, and attachment names");
        }

        const Value* attachment_weights = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                edit_value,
                "weights",
                Value::Type::Array,
                edit_path,
                &attachment_weights)) {
            return error;
        }
        if (const auto error = parse_mesh_weight_vertices(
                document,
                *attachment_weights,
                edit_path + ".weights",
                &edit.vertices)) {
            return error;
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_draw_order_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<DrawOrderKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "draw order timeline edits must contain at least one keyframe");
    }

    std::vector<DrawOrderKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        DrawOrderKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }

        const Value* slots_value = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                keyframe_value,
                "slots",
                Value::Type::Array,
                keyframe_path,
                &slots_value)) {
            return error;
        }
        if (const auto error = parse_string_array(
                document,
                *slots_value,
                keyframe_path + ".slots",
                &keyframe.slot_names)) {
            return error;
        }

        std::vector<std::string> sorted_slot_names = keyframe.slot_names;
        std::sort(sorted_slot_names.begin(), sorted_slot_names.end());
        if (std::adjacent_find(sorted_slot_names.begin(), sorted_slot_names.end()) !=
            sorted_slot_names.end()) {
            return validation_error(
                document,
                slots_value->location(),
                keyframe_path + ".slots",
                "draw order keyframes must not repeat slot names");
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "draw order timeline edit keyframe times must be strictly increasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_draw_order_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<DrawOrderTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<DrawOrderTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path = "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* draw_order = find_optional_member(animation_value, "drawOrder");
        if (draw_order == nullptr) {
            continue;
        }

        DrawOrderTimelineEdit edit;
        edit.animation_name = animation_name;
        if (const auto error = parse_draw_order_keyframes(
                document,
                *draw_order,
                animation_path + ".drawOrder",
                &edit.keyframes)) {
            return error;
        }
        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_event_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<EventKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "event timeline edits must contain at least one keyframe");
    }

    std::vector<EventKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;

    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        EventKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }
        if (const auto error = read_required_string(
                document, keyframe_value, "name", keyframe_path, &keyframe.event_name)) {
            return error;
        }
        if (keyframe.event_name.empty()) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".name",
                "event timeline edit names must not be empty");
        }

        int int_value = 0;
        if (const auto error = read_optional_integer(
                document, keyframe_value, "int", keyframe_path, &int_value)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "int") != nullptr) {
            keyframe.int_value = int_value;
        }

        double float_value = 0.0;
        if (const auto error = read_optional_number(
                document, keyframe_value, "float", keyframe_path, &float_value)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "float") != nullptr) {
            keyframe.float_value = float_value;
        }

        std::string string_value;
        if (const auto error = read_optional_string(
                document, keyframe_value, "string", keyframe_path, &string_value)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "string") != nullptr) {
            keyframe.string_value = std::move(string_value);
        }

        std::string audio_path;
        if (const auto error = read_optional_string(
                document, keyframe_value, "audio", keyframe_path, &audio_path)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "audio") != nullptr) {
            keyframe.audio_path = std::move(audio_path);
        }

        double volume = 0.0;
        if (const auto error = read_optional_number(
                document, keyframe_value, "volume", keyframe_path, &volume)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "volume") != nullptr) {
            keyframe.volume = volume;
        }

        double balance = 0.0;
        if (const auto error = read_optional_number(
                document, keyframe_value, "balance", keyframe_path, &balance)) {
            return error;
        }
        if (find_optional_member(keyframe_value, "balance") != nullptr) {
            keyframe.balance = balance;
        }

        if (has_previous_time && keyframe.time < previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "event timeline edit keyframe times must be non-decreasing");
        }

        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_event_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<EventTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<EventTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path = "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* events = find_optional_member(animation_value, "events");
        if (events == nullptr) {
            continue;
        }

        EventTimelineEdit edit;
        edit.animation_name = animation_name;
        if (const auto error = parse_event_keyframes(
                document,
                *events,
                animation_path + ".events",
                &edit.keyframes)) {
            return error;
        }
        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_slot_color_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<SlotColorKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "slot color timeline edits must contain at least one keyframe");
    }

    std::vector<SlotColorKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;
    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        SlotColorKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }
        const Value* color_value = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                keyframe_value,
                "color",
                Value::Type::Object,
                keyframe_path,
                &color_value)) {
            return error;
        }
        double r = 1.0;
        double g = 1.0;
        double b = 1.0;
        double a = 1.0;
        if (const auto error = read_required_number(document, *color_value, "r", keyframe_path + ".color", &r)) {
            return error;
        }
        if (const auto error = read_required_number(document, *color_value, "g", keyframe_path + ".color", &g)) {
            return error;
        }
        if (const auto error = read_required_number(document, *color_value, "b", keyframe_path + ".color", &b)) {
            return error;
        }
        if (const auto error = read_required_number(document, *color_value, "a", keyframe_path + ".color", &a)) {
            return error;
        }
        keyframe.color = runtime::SlotColor{r, g, b, a};
        if (const auto error = parse_interpolation(
                document, keyframe_value, keyframe_path, &keyframe.interpolation)) {
            return error;
        }
        if (const auto error = parse_curve_intent(
                document,
                keyframe_value,
                keyframe_path,
                TimelineKeyKind::SlotColor,
                TransformTimelineChannel::Rotate,
                &keyframe.curve_mode,
                &keyframe.curve_driver)) {
            return error;
        }
        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "slot color timeline edit keyframe times must be strictly increasing");
        }
        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

std::optional<LoadError> parse_slot_attachment_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<SlotAttachmentKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "slot attachment timeline edits must contain at least one keyframe");
    }

    std::vector<SlotAttachmentKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;
    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        SlotAttachmentKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }
        const Value* attachment_value = find_optional_member(keyframe_value, "attachment");
        if (attachment_value == nullptr) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".attachment",
                "slot attachment keyframes require attachment string or null");
        }
        if (attachment_value->is_string()) {
            keyframe.attachment_name = attachment_value->as_string();
        } else if (!attachment_value->is_null()) {
            return validation_error(
                document,
                attachment_value->location(),
                keyframe_path + ".attachment",
                "attachment must be a string or null");
        }
        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "slot attachment timeline edit keyframe times must be strictly increasing");
        }
        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(std::move(keyframe));
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

/**
 * @brief Parses one project-owned stepped inherit timeline.
 *
 * Two rules here are strictly stronger than the runtime's own parser and are
 * deliberate. The runtime accepts a NEGATIVE first key -- its
 * `has_previous_time` starts false, so nothing compares the first key against
 * zero -- and it reads exactly `time` and `inherit`, silently ignoring a
 * `curve` member. The overlay refuses both: the editor is the only thing that
 * writes one, and a `.marrow` keyframe object has never preserved unknown
 * members, so accepting easing here would mean dropping it on the next save.
 * Adding either rule to the runtime parser instead would make previously valid
 * `.mskl` files stop loading, which the story's format-version guarantee
 * forbids.
 */
std::optional<LoadError> parse_inherit_keyframes(
    const Document& document,
    const Value& timeline_value,
    std::string_view json_path,
    std::vector<InheritKeyframeEdit>* keyframes_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, timeline_value, Value::Type::Array, json_path)) {
        return error;
    }
    if (timeline_value.as_array().empty()) {
        return validation_error(
            document,
            timeline_value.location(),
            std::string(json_path),
            "inherit timeline edits must contain at least one keyframe");
    }

    std::vector<InheritKeyframeEdit> keyframes;
    keyframes.reserve(timeline_value.as_array().size());
    double previous_time = 0.0;
    bool has_previous_time = false;
    for (std::size_t keyframe_index = 0;
         keyframe_index < timeline_value.as_array().size();
         ++keyframe_index) {
        const Value& keyframe_value = timeline_value.as_array()[keyframe_index];
        const std::string keyframe_path =
            std::string(json_path) + "[" + std::to_string(keyframe_index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, keyframe_value, Value::Type::Object, keyframe_path)) {
            return error;
        }

        InheritKeyframeEdit keyframe;
        if (const auto error = read_required_number(
                document, keyframe_value, "time", keyframe_path, &keyframe.time)) {
            return error;
        }
        if (!finite_animation_scalar(keyframe.time) || keyframe.time < 0.0) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "inherit keyframe time must be finite and non-negative");
        }

        const Value* mode_value = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document,
                keyframe_value,
                "inherit",
                Value::Type::String,
                keyframe_path,
                &mode_value)) {
            return error;
        }
        const auto inherit = inherit_mode_from_key_impl(mode_value->as_string());
        if (!inherit.has_value()) {
            return validation_error(
                document,
                mode_value->location(),
                keyframe_path + ".inherit",
                std::string(kInheritModeMessage));
        }
        keyframe.inherit = *inherit;

        if (const Value* curve_value = find_optional_member(keyframe_value, "curve");
            curve_value != nullptr) {
            return validation_error(
                document,
                curve_value->location(),
                keyframe_path + ".curve",
                "inherit keys are stepped and must not carry curve data");
        }

        if (has_previous_time && keyframe.time <= previous_time) {
            return validation_error(
                document,
                keyframe_value.location(),
                keyframe_path + ".time",
                "inherit timeline edit keyframe times must be strictly increasing");
        }
        previous_time = keyframe.time;
        has_previous_time = true;
        keyframes.push_back(keyframe);
    }

    *keyframes_out = std::move(keyframes);
    return std::nullopt;
}

/**
 * @brief Collects every `timeline_edits.animations.<a>.bones.<b>.inherit` lane.
 *
 * Walks the same tree `parse_transform_timeline_edits` walks and picks only the
 * `inherit` member, skipping the four transform channel keys rather than
 * erroring on them -- the mirror image of the transform parser, which has
 * always skipped `inherit`.
 */
std::optional<LoadError> parse_bone_inherit_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<BoneInheritTimelineEdit>* edits_out) {
    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }

    const Value* animations = nullptr;
    if (const auto error = marrow::runtime::json::require_member(
            document,
            *timeline_edits,
            "animations",
            Value::Type::Object,
            "$.timeline_edits",
            &animations)) {
        return error;
    }

    std::vector<BoneInheritTimelineEdit> edits;
    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path =
            "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }

        const Value* bones = find_optional_member(animation_value, "bones");
        if (bones == nullptr) {
            continue;
        }
        if (const auto error = marrow::runtime::json::require_type(
                document, *bones, Value::Type::Object, animation_path + ".bones")) {
            return error;
        }

        for (const auto& [bone_name, bone_value] : bones->as_object()) {
            const std::string bone_path = animation_path + ".bones." + bone_name;
            if (const auto error = marrow::runtime::json::require_type(
                    document, bone_value, Value::Type::Object, bone_path)) {
                return error;
            }

            const Value* timeline_value = find_optional_member(bone_value, "inherit");
            if (timeline_value == nullptr) {
                continue;
            }

            BoneInheritTimelineEdit edit;
            edit.animation_name = animation_name;
            edit.bone_name = bone_name;
            if (const auto error = parse_inherit_keyframes(
                    document,
                    *timeline_value,
                    bone_path + ".inherit",
                    &edit.keyframes)) {
                return error;
            }
            edits.push_back(std::move(edit));
        }
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_slot_timeline_edits(
    const Document& document,
    const Value& root,
    std::vector<SlotColorTimelineEdit>* color_edits_out,
    std::vector<SlotAttachmentTimelineEdit>* attachment_edits_out) {
    color_edits_out->clear();
    attachment_edits_out->clear();

    const Value* timeline_edits = find_optional_member(root, "timeline_edits");
    if (timeline_edits == nullptr) {
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *timeline_edits, Value::Type::Object, "$.timeline_edits")) {
        return error;
    }
    const Value* animations = find_optional_member(*timeline_edits, "animations");
    if (animations == nullptr) {
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *animations, Value::Type::Object, "$.timeline_edits.animations")) {
        return error;
    }

    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path =
            "$.timeline_edits.animations." + animation_name;
        if (const auto error = marrow::runtime::json::require_type(
                document, animation_value, Value::Type::Object, animation_path)) {
            return error;
        }
        const Value* slots = find_optional_member(animation_value, "slots");
        if (slots == nullptr) {
            continue;
        }
        if (const auto error = marrow::runtime::json::require_type(
                document, *slots, Value::Type::Object, animation_path + ".slots")) {
            return error;
        }
        for (const auto& [slot_name, slot_value] : slots->as_object()) {
            const std::string slot_path = animation_path + ".slots." + slot_name;
            if (const auto error = marrow::runtime::json::require_type(
                    document, slot_value, Value::Type::Object, slot_path)) {
                return error;
            }
            if (const Value* color = find_optional_member(slot_value, "color")) {
                SlotColorTimelineEdit edit;
                edit.animation_name = animation_name;
                edit.slot_name = slot_name;
                if (const auto error = parse_slot_color_keyframes(
                        document, *color, slot_path + ".color", &edit.keyframes)) {
                    return error;
                }
                color_edits_out->push_back(std::move(edit));
            }
            if (const Value* attachment = find_optional_member(slot_value, "attachment")) {
                SlotAttachmentTimelineEdit edit;
                edit.animation_name = animation_name;
                edit.slot_name = slot_name;
                if (const auto error = parse_slot_attachment_keyframes(
                        document, *attachment, slot_path + ".attachment", &edit.keyframes)) {
                    return error;
                }
                attachment_edits_out->push_back(std::move(edit));
            }
        }
    }

    return std::nullopt;
}

/**
 * @brief Scatters the optional top-level `loop_sync` tree into the lane flags.
 *
 * Runs after every `timeline_edits` parser, because every `true` leaf is a
 * cross-reference into a lane those parsers populate. The tree mirrors
 * `timeline_edits.animations` exactly, and every leaf is a boolean whose
 * absence and whose `false` both mean "opted out".
 */
std::optional<LoadError> parse_loop_sync(
    const Document& document,
    const Value& root,
    std::vector<TransformTimelineEdit>* transform_edits,
    std::vector<SlotColorTimelineEdit>* slot_color_edits,
    std::vector<MeshDeformTimelineEdit>* deform_edits) {
    constexpr double kLoopSyncKeyTimeEpsilon = 1e-6;
    const Value* block = find_optional_member(root, "loop_sync");
    if (block == nullptr) {
        return std::nullopt;
    }
    if (!block->is_object()) {
        return validation_error(
            document, block->location(), "$.loop_sync", "loop_sync must be an object");
    }
    const Value* animations = find_optional_member(*block, "animations");
    if (animations == nullptr || !animations->is_object()) {
        return validation_error(
            document,
            animations != nullptr ? animations->location() : block->location(),
            "$.loop_sync.animations",
            "loop_sync requires an animations object");
    }

    const auto require_object =
        [&](const Value& value, const std::string& path) -> std::optional<LoadError> {
        if (value.is_object()) {
            return std::nullopt;
        }
        return validation_error(
            document, value.location(), path, "loop_sync entries must be objects");
    };
    const auto read_leaf = [&](const Value& value,
                               const std::string& path,
                               bool* enabled_out) -> std::optional<LoadError> {
        if (!value.is_boolean()) {
            return validation_error(
                document, value.location(), path, "loop_sync entries must be booleans");
        }
        *enabled_out = value.as_boolean();
        return std::nullopt;
    };
    // A lane opted in at load must exist and must already satisfy the one
    // structural prerequisite a parser can see. The explicit-duration
    // prerequisite is deliberately not checked here: no animation catalog
    // exists yet and `animation_edits` in the same document can author the
    // very duration in question.
    const auto accept_lane = [&](auto* lane,
                                 const Value& value,
                                 const std::string& path) -> std::optional<LoadError> {
        if (lane == nullptr) {
            return validation_error(
                document,
                value.location(),
                path,
                "loop_sync requires a timeline edit for that lane");
        }
        if (lane->keyframes.empty() ||
            std::abs(lane->keyframes.front().time) > kLoopSyncKeyTimeEpsilon) {
            return validation_error(
                document,
                value.location(),
                path,
                "loop synchronized timelines require a key at time zero");
        }
        lane->loop_sync = true;
        return std::nullopt;
    };

    for (const auto& [animation_name, animation_value] : animations->as_object()) {
        const std::string animation_path =
            "$.loop_sync.animations." + animation_name;
        if (const auto error = require_object(animation_value, animation_path)) {
            return error;
        }
        for (const auto& [category, category_value] : animation_value.as_object()) {
            const std::string category_path = animation_path + "." + category;
            if (const auto error = require_object(category_value, category_path)) {
                return error;
            }
            if (category == "bones") {
                for (const auto& [bone_name, bone_value] : category_value.as_object()) {
                    const std::string bone_path = category_path + "." + bone_name;
                    if (const auto error = require_object(bone_value, bone_path)) {
                        return error;
                    }
                    for (const auto& [channel_key, leaf] : bone_value.as_object()) {
                        const std::string leaf_path = bone_path + "." + channel_key;
                        const auto channel = transform_channel_from_key(channel_key);
                        if (!channel.has_value()) {
                            return validation_error(
                                document,
                                leaf.location(),
                                leaf_path,
                                "loop_sync transform channel must be rotate, translate, "
                                "scale, or shear");
                        }
                        bool enabled = false;
                        if (const auto error = read_leaf(leaf, leaf_path, &enabled)) {
                            return error;
                        }
                        if (!enabled) {
                            continue;
                        }
                        TransformTimelineEdit* lane = nullptr;
                        for (TransformTimelineEdit& edit : *transform_edits) {
                            if (edit.animation_name == animation_name &&
                                edit.bone_name == bone_name && edit.channel == *channel) {
                                lane = &edit;
                                break;
                            }
                        }
                        if (const auto error = accept_lane(lane, leaf, leaf_path)) {
                            return error;
                        }
                    }
                }
            } else if (category == "slots") {
                for (const auto& [slot_name, slot_value] : category_value.as_object()) {
                    const std::string slot_path = category_path + "." + slot_name;
                    if (const auto error = require_object(slot_value, slot_path)) {
                        return error;
                    }
                    for (const auto& [channel_key, leaf] : slot_value.as_object()) {
                        const std::string leaf_path = slot_path + "." + channel_key;
                        if (channel_key != "color") {
                            return validation_error(
                                document,
                                leaf.location(),
                                leaf_path,
                                "loop_sync slot entries support only color");
                        }
                        bool enabled = false;
                        if (const auto error = read_leaf(leaf, leaf_path, &enabled)) {
                            return error;
                        }
                        if (!enabled) {
                            continue;
                        }
                        SlotColorTimelineEdit* lane = nullptr;
                        for (SlotColorTimelineEdit& edit : *slot_color_edits) {
                            if (edit.animation_name == animation_name &&
                                edit.slot_name == slot_name) {
                                lane = &edit;
                                break;
                            }
                        }
                        if (const auto error = accept_lane(lane, leaf, leaf_path)) {
                            return error;
                        }
                    }
                }
            } else if (category == "deform") {
                for (const auto& [slot_name, slot_value] : category_value.as_object()) {
                    const std::string slot_path = category_path + "." + slot_name;
                    if (const auto error = require_object(slot_value, slot_path)) {
                        return error;
                    }
                    for (const auto& [attachment_name, leaf] : slot_value.as_object()) {
                        const std::string leaf_path = slot_path + "." + attachment_name;
                        bool enabled = false;
                        if (const auto error = read_leaf(leaf, leaf_path, &enabled)) {
                            return error;
                        }
                        if (!enabled) {
                            continue;
                        }
                        MeshDeformTimelineEdit* lane = nullptr;
                        for (MeshDeformTimelineEdit& edit : *deform_edits) {
                            if (edit.animation_name == animation_name &&
                                edit.slot_name == slot_name &&
                                edit.attachment_name == attachment_name) {
                                lane = &edit;
                                break;
                            }
                        }
                        if (const auto error = accept_lane(lane, leaf, leaf_path)) {
                            return error;
                        }
                    }
                }
            } else {
                return validation_error(
                    document,
                    category_value.location(),
                    category_path,
                    "loop_sync entries must be objects");
            }
        }
    }
    return std::nullopt;
}

std::optional<LoadError> parse_ik_constraint_edits(
    const Document& document,
    const Value& root,
    std::vector<IkConstraintEdit>* edits_out) {
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* ik = find_optional_member(*constraint_edits, "ik");
    if (ik == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *ik, Value::Type::Array, "$.constraint_edits.ik")) {
        return error;
    }

    std::vector<IkConstraintEdit> edits;
    edits.reserve(ik->as_array().size());
    for (std::size_t index = 0; index < ik->as_array().size(); ++index) {
        const Value& constraint_value = ik->as_array()[index];
        const std::string path = "$.constraint_edits.ik[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, constraint_value, Value::Type::Object, path)) {
            return error;
        }

        IkConstraintEdit edit;
        if (const auto error = read_required_string(
                document, constraint_value, "name", path, &edit.name)) {
            return error;
        }
        if (edit.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "ik constraint edit names must not be empty");
        }
        if (const auto duplicate = std::find_if(
                edits.begin(),
                edits.end(),
                [&](const IkConstraintEdit& existing_edit) {
                    return existing_edit.name == edit.name;
                });
            duplicate != edits.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".name",
                "ik constraint edit names must be unique");
        }

        const Value* bones = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document, constraint_value, "bones", Value::Type::Array, path, &bones)) {
            return error;
        }
        if (const auto error = parse_string_array(document, *bones, path + ".bones", &edit.bone_names)) {
            return error;
        }
        if (edit.bone_names.empty() || edit.bone_names.size() > 2U) {
            return validation_error(
                document,
                bones->location(),
                path + ".bones",
                "ik constraint edits must target one or two bones");
        }
        {
            std::vector<std::string> sorted_names = edit.bone_names;
            std::sort(sorted_names.begin(), sorted_names.end());
            if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) !=
                sorted_names.end()) {
                return validation_error(
                    document,
                    bones->location(),
                    path + ".bones",
                    "ik constraint edit bones must be unique");
            }
        }
        if (const auto error = read_required_string(
                document, constraint_value, "target", path, &edit.target_bone_name)) {
            return error;
        }
        if (edit.target_bone_name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".target",
                "ik constraint edits require a target bone");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "mix", path, &edit.mix)) {
            return error;
        }
        if (edit.mix < 0.0 || edit.mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                path + ".mix",
                "ik constraint edit mix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "softness", path, &edit.softness)) {
            return error;
        }
        const Value* bend_positive = find_optional_member(constraint_value, "bendPositive");
        if (bend_positive != nullptr) {
            if (const auto error = marrow::runtime::json::require_type(
                    document,
                    *bend_positive,
                    Value::Type::Boolean,
                    path + ".bendPositive")) {
                return error;
            }
            edit.bend_positive = bend_positive->as_boolean();
        }
        if (const auto error = read_optional_boolean(
                document, constraint_value, "compress", path, &edit.compress)) {
            return error;
        }
        if (const auto error = read_optional_boolean(
                document, constraint_value, "stretch", path, &edit.stretch)) {
            return error;
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_path_constraint_edits(
    const Document& document,
    const Value& root,
    std::vector<PathConstraintEdit>* edits_out) {
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* path = find_optional_member(*constraint_edits, "path");
    if (path == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *path, Value::Type::Array, "$.constraint_edits.path")) {
        return error;
    }

    std::vector<PathConstraintEdit> edits;
    edits.reserve(path->as_array().size());
    for (std::size_t index = 0; index < path->as_array().size(); ++index) {
        const Value& constraint_value = path->as_array()[index];
        const std::string json_path = "$.constraint_edits.path[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, constraint_value, Value::Type::Object, json_path)) {
            return error;
        }

        PathConstraintEdit edit;
        if (const auto error = read_required_string(
                document, constraint_value, "name", json_path, &edit.name)) {
            return error;
        }
        if (edit.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "path constraint edit names must not be empty");
        }
        if (const auto duplicate = std::find_if(
                edits.begin(),
                edits.end(),
                [&](const PathConstraintEdit& existing_edit) {
                    return existing_edit.name == edit.name;
                });
            duplicate != edits.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "path constraint edit names must be unique");
        }
        if (const auto error = read_required_string(
                document, constraint_value, "slot", json_path, &edit.slot_name)) {
            return error;
        }
        if (edit.slot_name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".slot",
                "path constraint edits require a slot");
        }

        const Value* bones = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document, constraint_value, "bones", Value::Type::Array, json_path, &bones)) {
            return error;
        }
        if (const auto error =
                parse_string_array(document, *bones, json_path + ".bones", &edit.bone_names)) {
            return error;
        }
        if (edit.bone_names.empty()) {
            return validation_error(
                document,
                bones->location(),
                json_path + ".bones",
                "path constraint edits must target at least one bone");
        }
        {
            std::vector<std::string> sorted_names = edit.bone_names;
            std::sort(sorted_names.begin(), sorted_names.end());
            if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
                return validation_error(
                    document,
                    bones->location(),
                    json_path + ".bones",
                    "path constraint edit bones must be unique");
            }
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "position", json_path, &edit.position)) {
            return error;
        }
        if (edit.position < 0.0 || edit.position > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".position",
                "path constraint edit position must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "spacing", json_path, &edit.spacing)) {
            return error;
        }
        if (edit.spacing < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".spacing",
                "path constraint edit spacing must be non-negative");
        }

        std::string spacing_mode_name;
        if (const auto error = read_optional_string(
                document,
                constraint_value,
                "spacingMode",
                json_path,
                &spacing_mode_name)) {
            return error;
        }
        if (!spacing_mode_name.empty()) {
            const auto spacing_mode = path_spacing_mode_from_key(spacing_mode_name);
            if (!spacing_mode.has_value()) {
                return validation_error(
                    document,
                    constraint_value.location(),
                    json_path + ".spacingMode",
                    "path constraint edit spacingMode must be 'length' or 'percent'");
            }
            edit.spacing_mode = *spacing_mode;
        }

        if (const auto error = read_optional_number(
                document, constraint_value, "rotateMix", json_path, &edit.rotate_mix)) {
            return error;
        }
        if (edit.rotate_mix < 0.0 || edit.rotate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".rotateMix",
                "path constraint edit rotateMix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "translateMix", json_path, &edit.translate_mix)) {
            return error;
        }
        if (edit.translate_mix < 0.0 || edit.translate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".translateMix",
                "path constraint edit translateMix must stay within [0, 1]");
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_transform_constraint_edits(
    const Document& document,
    const Value& root,
    std::vector<TransformConstraintEdit>* edits_out) {
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* transform = find_optional_member(*constraint_edits, "transform");
    if (transform == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *transform, Value::Type::Array, "$.constraint_edits.transform")) {
        return error;
    }

    std::vector<TransformConstraintEdit> edits;
    edits.reserve(transform->as_array().size());
    for (std::size_t index = 0; index < transform->as_array().size(); ++index) {
        const Value& constraint_value = transform->as_array()[index];
        const std::string json_path =
            "$.constraint_edits.transform[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, constraint_value, Value::Type::Object, json_path)) {
            return error;
        }

        TransformConstraintEdit edit;
        if (const auto error = read_required_string(
                document, constraint_value, "name", json_path, &edit.name)) {
            return error;
        }
        if (edit.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "transform constraint edit names must not be empty");
        }
        if (const auto duplicate = std::find_if(
                edits.begin(),
                edits.end(),
                [&](const TransformConstraintEdit& existing_edit) {
                    return existing_edit.name == edit.name;
                });
            duplicate != edits.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "transform constraint edit names must be unique");
        }
        if (const auto error = read_required_string(
                document, constraint_value, "source", json_path, &edit.source_bone_name)) {
            return error;
        }
        if (edit.source_bone_name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".source",
                "transform constraint edits require a source bone");
        }

        const Value* bones = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document, constraint_value, "bones", Value::Type::Array, json_path, &bones)) {
            return error;
        }
        if (const auto error =
                parse_string_array(document, *bones, json_path + ".bones", &edit.bone_names)) {
            return error;
        }
        if (edit.bone_names.empty()) {
            return validation_error(
                document,
                bones->location(),
                json_path + ".bones",
                "transform constraint edits must target at least one bone");
        }
        {
            std::vector<std::string> sorted_names = edit.bone_names;
            std::sort(sorted_names.begin(), sorted_names.end());
            if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
                return validation_error(
                    document,
                    bones->location(),
                    json_path + ".bones",
                    "transform constraint edit bones must be unique");
            }
        }

        if (std::find(edit.bone_names.begin(), edit.bone_names.end(), edit.source_bone_name) !=
            edit.bone_names.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".bones",
                "transform constraint source bone must not also be a target");
        }

        if (const auto error = read_optional_number(
                document, constraint_value, "rotateMix", json_path, &edit.rotate_mix)) {
            return error;
        }
        if (edit.rotate_mix < 0.0 || edit.rotate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".rotateMix",
                "transform constraint edit rotateMix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "translateMix", json_path, &edit.translate_mix)) {
            return error;
        }
        if (edit.translate_mix < 0.0 || edit.translate_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".translateMix",
                "transform constraint edit translateMix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "scaleMix", json_path, &edit.scale_mix)) {
            return error;
        }
        if (edit.scale_mix < 0.0 || edit.scale_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".scaleMix",
                "transform constraint edit scaleMix must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "shearMix", json_path, &edit.shear_mix)) {
            return error;
        }
        if (edit.shear_mix < 0.0 || edit.shear_mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".shearMix",
                "transform constraint edit shearMix must stay within [0, 1]");
        }

        const Value* offset = find_optional_member(constraint_value, "offset");
        if (offset != nullptr) {
            if (const auto error = marrow::runtime::json::require_type(
                    document, *offset, Value::Type::Object, json_path + ".offset")) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "rotation", json_path + ".offset", &edit.offsets.rotation)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "x", json_path + ".offset", &edit.offsets.x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "y", json_path + ".offset", &edit.offsets.y)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "scaleX", json_path + ".offset", &edit.offsets.scale_x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "scaleY", json_path + ".offset", &edit.offsets.scale_y)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "shearX", json_path + ".offset", &edit.offsets.shear_x)) {
                return error;
            }
            if (const auto error = read_optional_number(
                    document, *offset, "shearY", json_path + ".offset", &edit.offsets.shear_y)) {
                return error;
            }
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_physics_constraint_edits(
    const Document& document,
    const Value& root,
    std::vector<PhysicsConstraintEdit>* edits_out) {
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* physics = find_optional_member(*constraint_edits, "physics");
    if (physics == nullptr) {
        edits_out->clear();
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *physics, Value::Type::Array, "$.constraint_edits.physics")) {
        return error;
    }

    std::vector<PhysicsConstraintEdit> edits;
    edits.reserve(physics->as_array().size());
    for (std::size_t index = 0; index < physics->as_array().size(); ++index) {
        const Value& constraint_value = physics->as_array()[index];
        const std::string json_path =
            "$.constraint_edits.physics[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, constraint_value, Value::Type::Object, json_path)) {
            return error;
        }

        PhysicsConstraintEdit edit;
        if (const auto error = read_required_string(
                document, constraint_value, "name", json_path, &edit.name)) {
            return error;
        }
        if (edit.name.empty()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "physics constraint edit names must not be empty");
        }
        if (const auto duplicate = std::find_if(
                edits.begin(),
                edits.end(),
                [&](const PhysicsConstraintEdit& existing_edit) {
                    return existing_edit.name == edit.name;
                });
            duplicate != edits.end()) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".name",
                "physics constraint edit names must be unique");
        }

        const Value* bones = nullptr;
        if (const auto error = marrow::runtime::json::require_member(
                document, constraint_value, "bones", Value::Type::Array, json_path, &bones)) {
            return error;
        }
        if (const auto error =
                parse_string_array(document, *bones, json_path + ".bones", &edit.bone_names)) {
            return error;
        }
        if (edit.bone_names.empty()) {
            return validation_error(
                document,
                bones->location(),
                json_path + ".bones",
                "physics constraint edits must target at least one bone");
        }
        {
            std::vector<std::string> sorted_names = edit.bone_names;
            std::sort(sorted_names.begin(), sorted_names.end());
            if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
                return validation_error(
                    document,
                    bones->location(),
                    json_path + ".bones",
                    "physics constraint edit bones must be unique");
            }
        }

        if (const auto error = read_optional_number(
                document, constraint_value, "step", json_path, &edit.step)) {
            return error;
        }
        if (edit.step <= 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".step",
                "physics constraint edit step must be greater than zero");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "x", json_path, &edit.x)) {
            return error;
        }
        if (edit.x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".x",
                "physics constraint edit x must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "y", json_path, &edit.y)) {
            return error;
        }
        if (edit.y < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".y",
                "physics constraint edit y must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "rotate", json_path, &edit.rotate)) {
            return error;
        }
        if (edit.rotate < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".rotate",
                "physics constraint edit rotate must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "scaleX", json_path, &edit.scale_x)) {
            return error;
        }
        if (edit.scale_x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".scaleX",
                "physics constraint edit scaleX must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "shearX", json_path, &edit.shear_x)) {
            return error;
        }
        if (edit.shear_x < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".shearX",
                "physics constraint edit shearX must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "limit", json_path, &edit.limit)) {
            return error;
        }
        if (edit.limit < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".limit",
                "physics constraint edit limit must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "inertia", json_path, &edit.inertia)) {
            return error;
        }
        if (edit.inertia < 0.0 || edit.inertia > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".inertia",
                "physics constraint edit inertia must stay within [0, 1]");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "damping", json_path, &edit.damping)) {
            return error;
        }
        if (edit.damping < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".damping",
                "physics constraint edit damping must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "strength", json_path, &edit.strength)) {
            return error;
        }
        if (edit.strength < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".strength",
                "physics constraint edit strength must be non-negative");
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "massInverse", json_path, &edit.mass_inverse)) {
            return error;
        }
        if (edit.mass_inverse < 0.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".massInverse",
                "physics constraint edit massInverse must be non-negative");
        }
        if (const auto error = parse_optional_xy_vector(
                document, constraint_value, "gravity", json_path, &edit.gravity)) {
            return error;
        }
        if (const auto error = parse_optional_xy_vector(
                document, constraint_value, "wind", json_path, &edit.wind)) {
            return error;
        }
        if (const auto error = read_optional_number(
                document, constraint_value, "mix", json_path, &edit.mix)) {
            return error;
        }
        if (edit.mix < 0.0 || edit.mix > 1.0) {
            return validation_error(
                document,
                constraint_value.location(),
                json_path + ".mix",
                "physics constraint edit mix must stay within [0, 1]");
        }

        edits.push_back(std::move(edit));
    }

    *edits_out = std::move(edits);
    return std::nullopt;
}

std::optional<LoadError> parse_constraint_lifecycle_operations(
    const Document& document,
    const Value& root,
    std::vector<ConstraintLifecycleOperation>* operations_out) {
    operations_out->clear();
    const Value* constraint_edits = find_optional_member(root, "constraint_edits");
    if (constraint_edits == nullptr) {
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document, *constraint_edits, Value::Type::Object, "$.constraint_edits")) {
        return error;
    }

    const Value* operations = find_optional_member(*constraint_edits, "operations");
    if (operations == nullptr) {
        return std::nullopt;
    }
    if (const auto error = marrow::runtime::json::require_type(
            document,
            *operations,
            Value::Type::Array,
            "$.constraint_edits.operations")) {
        return error;
    }

    std::vector<ConstraintLifecycleOperation> parsed;
    parsed.reserve(operations->as_array().size());
    for (std::size_t index = 0; index < operations->as_array().size(); ++index) {
        const Value& record = operations->as_array()[index];
        const std::string path =
            "$.constraint_edits.operations[" + std::to_string(index) + "]";
        if (const auto error = marrow::runtime::json::require_type(
                document, record, Value::Type::Object, path)) {
            return error;
        }

        ConstraintLifecycleOperation operation;

        std::string op;
        if (const auto error = read_required_string(document, record, "op", path, &op)) {
            return error;
        }
        if (op == "rename") {
            operation.kind = ConstraintLifecycleKind::Rename;
        } else if (op == "delete") {
            operation.kind = ConstraintLifecycleKind::Delete;
        } else {
            return validation_error(
                document,
                record.location(),
                path + ".op",
                "constraint lifecycle op must be 'rename' or 'delete'");
        }

        std::string family;
        if (const auto error =
                read_required_string(document, record, "family", path, &family)) {
            return error;
        }
        const auto parsed_family = constraint_family_from_json_key(family);
        if (!parsed_family.has_value()) {
            return validation_error(
                document,
                record.location(),
                path + ".family",
                "constraint lifecycle family must be one of 'ik', 'path', "
                "'transform', 'physics'");
        }
        operation.family = *parsed_family;

        // Rejecting the key that belongs to the *other* op, rather than
        // ignoring it, is deliberate: a silently ignored key is how a future
        // writer's typo becomes a silent no-op.
        if (operation.kind == ConstraintLifecycleKind::Rename) {
            if (find_optional_member(record, "name") != nullptr) {
                return validation_error(
                    document,
                    record.location(),
                    path + ".name",
                    "constraint lifecycle rename records must not carry 'name'; "
                    "use 'from' and 'to'");
            }
            if (const auto error =
                    read_required_string(document, record, "from", path, &operation.name)) {
                return error;
            }
            if (operation.name.empty()) {
                return validation_error(
                    document,
                    record.location(),
                    path + ".from",
                    "constraint lifecycle rename source must not be empty");
            }
            if (const auto error = read_required_string(
                    document, record, "to", path, &operation.new_name)) {
                return error;
            }
            if (operation.new_name.empty()) {
                return validation_error(
                    document,
                    record.location(),
                    path + ".to",
                    "constraint lifecycle rename target must not be empty");
            }
            if (operation.new_name == operation.name) {
                return validation_error(
                    document,
                    record.location(),
                    path + ".to",
                    "constraint lifecycle rename target must differ from its source");
            }
        } else {
            if (find_optional_member(record, "from") != nullptr) {
                return validation_error(
                    document,
                    record.location(),
                    path + ".from",
                    "constraint lifecycle delete records must not carry 'from'; "
                    "use 'name'");
            }
            if (find_optional_member(record, "to") != nullptr) {
                return validation_error(
                    document,
                    record.location(),
                    path + ".to",
                    "constraint lifecycle delete records must not carry 'to'; "
                    "use 'name'");
            }
            if (const auto error =
                    read_required_string(document, record, "name", path, &operation.name)) {
                return error;
            }
            if (operation.name.empty()) {
                return validation_error(
                    document,
                    record.location(),
                    path + ".name",
                    "constraint lifecycle delete target must not be empty");
            }
        }

        parsed.push_back(std::move(operation));
    }

    *operations_out = std::move(parsed);
    return std::nullopt;
}

} // namespace

namespace project_detail {

std::optional<LoadError> parse_project_document(
    const Document& document, ProjectData* project_out) {
    if (const auto error = marrow::runtime::json::require_type(
            document, document.root, Value::Type::Object, "$")) {
        return error;
    }

    ProjectData project;
    project.source_path = document.source_path;
    if (const auto error = read_required_string(
            document, document.root, "marrow", "$", &project.marrow_version)) {
        return error;
    }
    if (project.marrow_version.empty()) {
        return validation_error(
            document,
            document.root.location(),
            "$.marrow",
            "version must not be empty");
    }

    if (const auto error = parse_runtime_assets(document, document.root, &project.runtime_assets)) {
        return error;
    }
    if (const auto error = parse_editor_metadata(document, document.root, &project.editor_metadata)) {
        return error;
    }
    if (const auto error = parse_snap_settings(
            document, document.root, &project.snap_settings)) {
        return error;
    }
    if (const auto error = parse_animation_edits(
            document, document.root, &project.animation_edits)) {
        return error;
    }
    if (const auto error = parse_transform_timeline_edits(
            document, document.root, &project.transform_timeline_edits)) {
        return error;
    }
    // Runs with the other `timeline_edits` parsers and BEFORE `parse_loop_sync`
    // below, whose leaves cross-reference the lanes these parsers populate.
    // Inherit authors no loop-sync leaf, but the ordering invariant that
    // comment states is not weakened by an exception.
    if (const auto error = parse_bone_inherit_timeline_edits(
            document, document.root, &project.bone_inherit_timeline_edits)) {
        return error;
    }
    if (const auto error = parse_mesh_deform_timeline_edits(
            document, document.root, &project.mesh_deform_timeline_edits)) {
        return error;
    }
    if (const auto error = parse_mesh_weight_attachment_edits(
            document, document.root, &project.mesh_weight_attachment_edits)) {
        return error;
    }
    if (const auto error = parse_draw_order_timeline_edits(
            document, document.root, &project.draw_order_timeline_edits)) {
        return error;
    }
    if (const auto error = parse_event_timeline_edits(
            document, document.root, &project.event_timeline_edits)) {
        return error;
    }
    if (const auto error = parse_slot_timeline_edits(
            document,
            document.root,
            &project.slot_color_timeline_edits,
            &project.slot_attachment_timeline_edits)) {
        return error;
    }
    // MAR-172 runs after every timeline_edits parser: each `true` leaf is a
    // cross-reference into a lane those parsers populate.
    if (const auto error = parse_loop_sync(
            document,
            document.root,
            &project.transform_timeline_edits,
            &project.slot_color_timeline_edits,
            &project.mesh_deform_timeline_edits)) {
        return error;
    }
    if (const auto error = parse_ik_constraint_edits(
            document, document.root, &project.ik_constraint_edits)) {
        return error;
    }
    if (const auto error = parse_path_constraint_edits(
            document, document.root, &project.path_constraint_edits)) {
        return error;
    }
    if (const auto error = parse_transform_constraint_edits(
            document, document.root, &project.transform_constraint_edits)) {
        return error;
    }
    if (const auto error = parse_physics_constraint_edits(
            document, document.root, &project.physics_constraint_edits)) {
        return error;
    }
    if (const auto error = parse_constraint_lifecycle_operations(
            document, document.root, &project.constraint_lifecycle_operations)) {
        return error;
    }
    if (const auto error = parse_parameter_model(
            document, document.root, &project.parameter_model)) {
        return error;
    }
    if (const auto error = parse_atlas_pack_definitions(
            document, document.root, &project.atlas_pack_definitions)) {
        return error;
    }

    for (std::size_t definition_index = 0;
         definition_index < project.atlas_pack_definitions.size();
         ++definition_index) {
        const AtlasPackDefinition& definition = project.atlas_pack_definitions[definition_index];
        const std::filesystem::path resolved_definition_path =
            project.resolve_path(definition.atlas_path);
        const auto matching_atlas = std::find_if(
            project.runtime_assets.atlas_paths.begin(),
            project.runtime_assets.atlas_paths.end(),
            [&](const std::filesystem::path& atlas_path) {
                return project.resolve_path(atlas_path) == resolved_definition_path;
            });
        if (matching_atlas == project.runtime_assets.atlas_paths.end()) {
            return validation_error(
                document,
                document.root.location(),
                "$.atlas_packs[" + std::to_string(definition_index) + "].atlas",
                "atlas pack outputs must also appear in runtime.atlases");
        }
    }

    project.preserved_root = document.root;

    *project_out = std::move(project);
    return std::nullopt;
}

} // namespace project_detail

} // namespace marrow::editor
