#include "skeleton_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace marrow::runtime::detail {
namespace {

using json::Document;
using json::LoadError;
using json::Value;

constexpr double kParameterCoordinateEpsilon = 1e-9;

bool parameter_nearly_equal(double lhs, double rhs) {
    return std::abs(lhs - rhs) <=
        kParameterCoordinateEpsilon * std::max({1.0, std::abs(lhs), std::abs(rhs)});
}

bool has_effective_path_segment(const std::vector<AttachmentVertex>& points) {
    for (std::size_t index = 1; index < points.size(); ++index) {
        if (!parameter_nearly_equal(points[index - 1U].x, points[index].x) ||
            !parameter_nearly_equal(points[index - 1U].y, points[index].y)) {
            return true;
        }
    }
    return false;
}

bool shape_targets_overlap(
    const std::vector<SkinData>& skins,
    std::size_t slot_index,
    std::string_view lhs,
    std::string_view rhs) {
    for (const SkinData& skin : skins) {
        for (const SkinSlotData& candidate : skin.slot_attachments) {
            if (candidate.slot_index == slot_index &&
                attachment_matches_mesh_deform_source(candidate.attachment, lhs) &&
                attachment_matches_mesh_deform_source(candidate.attachment, rhs)) {
                return true;
            }
        }
    }
    return false;
}

bool slot_has_mesh_attachment(
    const std::vector<SkinData>& skins,
    std::size_t slot_index) {
    for (const SkinData& skin : skins) {
        for (const SkinSlotData& candidate : skin.slot_attachments) {
            if (candidate.slot_index == slot_index &&
                candidate.attachment.mesh_geometry != nullptr) {
                return true;
            }
        }
    }
    return false;
}

LoadError parameter_error(
    const Document& document,
    const Value& value,
    std::string path,
    std::string message) {
    return json::make_validation_error(
        document,
        value.location(),
        std::move(path),
        std::move(message));
}

const Value* optional_member(const Value& object, std::string_view key) {
    return object.is_object() ? json::find_member(object, key) : nullptr;
}

std::optional<LoadError> required_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view path,
    std::string* output) {
    const Value* member = nullptr;
    if (const auto error = json::require_member(
            document, object, key, Value::Type::String, path, &member)) {
        return error;
    }
    *output = member->as_string();
    if (output->empty()) {
        return parameter_error(
            document,
            *member,
            std::string(path) + "." + std::string(key),
            "value must not be empty");
    }
    return std::nullopt;
}

std::optional<LoadError> optional_string(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view path,
    std::optional<std::string>* output) {
    const Value* member = optional_member(object, key);
    if (member == nullptr) {
        output->reset();
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::String,
            std::string(path) + "." + std::string(key))) {
        return error;
    }
    if (member->as_string().empty()) {
        return parameter_error(
            document,
            *member,
            std::string(path) + "." + std::string(key),
            "value must not be empty");
    }
    *output = member->as_string();
    return std::nullopt;
}

std::optional<LoadError> required_number(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view path,
    double* output) {
    const Value* member = nullptr;
    if (const auto error = json::require_member(
            document, object, key, Value::Type::Number, path, &member)) {
        return error;
    }
    if (!std::isfinite(member->as_number())) {
        return parameter_error(
            document,
            *member,
            std::string(path) + "." + std::string(key),
            "number must be finite");
    }
    *output = member->as_number();
    return std::nullopt;
}

std::optional<LoadError> optional_number(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view path,
    double* output) {
    const Value* member = optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::Number,
            std::string(path) + "." + std::string(key))) {
        return error;
    }
    if (!std::isfinite(member->as_number())) {
        return parameter_error(
            document,
            *member,
            std::string(path) + "." + std::string(key),
            "number must be finite");
    }
    *output = member->as_number();
    return std::nullopt;
}

std::optional<LoadError> required_boolean(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view path,
    bool* output) {
    const Value* member = nullptr;
    if (const auto error = json::require_member(
            document, object, key, Value::Type::Boolean, path, &member)) {
        return error;
    }
    *output = member->as_boolean();
    return std::nullopt;
}

std::optional<LoadError> optional_boolean(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view path,
    bool* output) {
    const Value* member = optional_member(object, key);
    if (member == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document,
            *member,
            Value::Type::Boolean,
            std::string(path) + "." + std::string(key))) {
        return error;
    }
    *output = member->as_boolean();
    return std::nullopt;
}

std::optional<LoadError> required_size(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view path,
    std::size_t* output) {
    double number = 0.0;
    if (const auto error = required_number(document, object, key, path, &number)) {
        return error;
    }
    if (number < 0.0 || std::floor(number) != number ||
        number > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        const Value* member = json::find_member(object, key);
        return parameter_error(
            document,
            *member,
            std::string(path) + "." + std::string(key),
            "value must be a non-negative integer");
    }
    *output = static_cast<std::size_t>(number);
    return std::nullopt;
}

std::optional<LoadError> required_int(
    const Document& document,
    const Value& object,
    std::string_view key,
    std::string_view path,
    int* output) {
    double number = 0.0;
    if (const auto error = required_number(document, object, key, path, &number)) {
        return error;
    }
    if (std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        const Value* member = json::find_member(object, key);
        return parameter_error(
            document,
            *member,
            std::string(path) + "." + std::string(key),
            "value must be an integer in the runtime int range");
    }
    *output = static_cast<int>(number);
    return std::nullopt;
}

std::optional<LoadError> parse_number_array(
    const Document& document,
    const Value& value,
    std::string_view path,
    std::vector<double>* output) {
    if (const auto error = json::require_type(document, value, Value::Type::Array, path)) {
        return error;
    }
    output->clear();
    output->reserve(value.as_array().size());
    for (std::size_t index = 0; index < value.as_array().size(); ++index) {
        const Value& element = value.as_array()[index];
        const std::string element_path =
            std::string(path) + "[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, element, Value::Type::Number, element_path)) {
            return error;
        }
        if (!std::isfinite(element.as_number())) {
            return parameter_error(document, element, element_path, "number must be finite");
        }
        output->push_back(element.as_number());
    }
    return std::nullopt;
}

std::optional<LoadError> parse_points(
    const Document& document,
    const Value& value,
    std::string_view path,
    std::size_t minimum_points,
    std::vector<AttachmentVertex>* output) {
    std::vector<double> coordinates;
    if (const auto error = parse_number_array(document, value, path, &coordinates)) {
        return error;
    }
    if (coordinates.size() % 2U != 0U || coordinates.size() / 2U < minimum_points) {
        return parameter_error(
            document,
            value,
            std::string(path),
            "points must be a flat x/y array with at least " +
                std::to_string(minimum_points) + " points");
    }
    output->clear();
    output->reserve(coordinates.size() / 2U);
    for (std::size_t index = 0; index < coordinates.size(); index += 2U) {
        output->push_back({coordinates[index], coordinates[index + 1U]});
    }
    return std::nullopt;
}

std::optional<LoadError> parse_color(
    const Document& document,
    const Value& value,
    std::string_view path,
    SlotColor* output) {
    if (const auto error = json::require_type(document, value, Value::Type::Object, path)) {
        return error;
    }
    double components[4] = {};
    constexpr std::string_view names[] = {"r", "g", "b", "a"};
    for (std::size_t index = 0; index < 4U; ++index) {
        if (const auto error = required_number(
                document, value, names[index], path, &components[index])) {
            return error;
        }
        if (components[index] < 0.0 || components[index] > 1.0) {
            return parameter_error(
                document,
                *json::find_member(value, names[index]),
                std::string(path) + "." + std::string(names[index]),
                "color component must stay within [0, 1]");
        }
    }
    *output = SlotColor{components[0], components[1], components[2], components[3]};
    return std::nullopt;
}

std::optional<LoadError> parse_string_array(
    const Document& document,
    const Value& value,
    std::string_view path,
    std::vector<std::string>* output) {
    if (const auto error = json::require_type(document, value, Value::Type::Array, path)) {
        return error;
    }
    output->clear();
    output->reserve(value.as_array().size());
    std::unordered_set<std::string> unique;
    for (std::size_t index = 0; index < value.as_array().size(); ++index) {
        const Value& item = value.as_array()[index];
        const std::string item_path =
            std::string(path) + "[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, item, Value::Type::String, item_path)) {
            return error;
        }
        if (item.as_string().empty()) {
            return parameter_error(document, item, item_path, "id must not be empty");
        }
        if (!unique.insert(item.as_string()).second) {
            return parameter_error(document, item, item_path, "duplicate id in ordered list");
        }
        output->push_back(item.as_string());
    }
    return std::nullopt;
}

std::optional<LoadError> optional_root_array(
    const Document& document,
    const Value& root,
    std::string_view key,
    const Value** output) {
    *output = json::find_member(root, key);
    if (*output == nullptr) {
        return std::nullopt;
    }
    return json::require_type(
        document,
        **output,
        Value::Type::Array,
        "$." + std::string(key));
}

std::optional<LoadError> parse_parameters(
    const Document& document,
    const Value& root,
    ParameterModelDefinitions* model) {
    const Value* values = nullptr;
    if (const auto error = optional_root_array(document, root, "parameters", &values)) {
        return error;
    }
    if (values == nullptr) {
        return std::nullopt;
    }

    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values->as_array().size(); ++index) {
        const Value& value = values->as_array()[index];
        const std::string path = "$.parameters[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, value, Value::Type::Object, path)) {
            return error;
        }
        ParameterDefinition parameter;
        if (const auto error = required_string(document, value, "id", path, &parameter.id)) {
            return error;
        }
        if (!ids.insert(parameter.id).second) {
            return parameter_error(document, value, path + ".id", "duplicate parameter id");
        }
        if (const auto error = required_string(document, value, "name", path, &parameter.name)) {
            return error;
        }
        if (const auto error = required_number(
                document, value, "min", path, &parameter.min_value)) {
            return error;
        }
        if (const auto error = required_number(
                document, value, "max", path, &parameter.max_value)) {
            return error;
        }
        if (parameter.min_value > parameter.max_value) {
            return parameter_error(document, value, path, "parameter min must not exceed max");
        }
        if (const auto error = required_number(
                document, value, "default", path, &parameter.default_value)) {
            return error;
        }
        std::string type;
        if (const auto error = required_string(document, value, "type", path, &type)) {
            return error;
        }
        if (type == "continuous") {
            parameter.type = ParameterType::Continuous;
        } else if (type == "discrete") {
            parameter.type = ParameterType::Discrete;
        } else {
            return parameter_error(
                document,
                *json::find_member(value, "type"),
                path + ".type",
                "parameter type must be 'continuous' or 'discrete'");
        }
        if (const auto error = required_boolean(
                document, value, "clamp", path, &parameter.clamp)) {
            return error;
        }
        if (parameter.clamp &&
            (parameter.default_value < parameter.min_value ||
             parameter.default_value > parameter.max_value)) {
            return parameter_error(
                document,
                *json::find_member(value, "default"),
                path + ".default",
                "clamped parameter default must stay inside [min, max]");
        }
        if (const Value* ui_step = optional_member(value, "ui_step")) {
            double parsed = 0.0;
            if (const auto error = optional_number(
                    document, value, "ui_step", path, &parsed)) {
                return error;
            }
            if (parsed <= 0.0) {
                return parameter_error(
                    document, *ui_step, path + ".ui_step", "ui_step must be positive");
            }
            parameter.ui_step = parsed;
        }
        if (const auto error = optional_string(
                document, value, "units", path, &parameter.units)) {
            return error;
        }
        model->parameters.push_back(std::move(parameter));
    }
    return std::nullopt;
}

std::optional<LoadError> parse_parameter_groups(
    const Document& document,
    const Value& root,
    const std::unordered_set<std::string>& parameter_ids,
    ParameterModelDefinitions* model) {
    const Value* values = nullptr;
    if (const auto error = optional_root_array(document, root, "parameterGroups", &values)) {
        return error;
    }
    if (values == nullptr) {
        return std::nullopt;
    }
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values->as_array().size(); ++index) {
        const Value& value = values->as_array()[index];
        const std::string path = "$.parameterGroups[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, value, Value::Type::Object, path)) {
            return error;
        }
        ParameterGroupDefinition group;
        if (const auto error = required_string(document, value, "id", path, &group.id)) {
            return error;
        }
        if (!ids.insert(group.id).second) {
            return parameter_error(document, value, path + ".id", "duplicate group id");
        }
        if (const auto error = required_string(document, value, "name", path, &group.name)) {
            return error;
        }
        const Value* parameters = nullptr;
        if (const auto error = json::require_member(
                document,
                value,
                "parameters",
                Value::Type::Array,
                path,
                &parameters)) {
            return error;
        }
        if (const auto error = parse_string_array(
                document, *parameters, path + ".parameters", &group.parameter_ids)) {
            return error;
        }
        for (const std::string& parameter_id : group.parameter_ids) {
            if (parameter_ids.find(parameter_id) == parameter_ids.end()) {
                return parameter_error(
                    document,
                    *parameters,
                    path + ".parameters",
                    "group references unknown parameter '" + parameter_id + "'");
            }
        }
        if (const auto error = optional_boolean(
                document, value, "collapsed", path, &group.collapsed)) {
            return error;
        }
        if (const auto error = optional_string(
                document, value, "color_tag", path, &group.color_tag)) {
            return error;
        }
        if (const auto error = optional_string(
                document, value, "exclusive_mode", path, &group.exclusive_mode)) {
            return error;
        }
        model->parameter_groups.push_back(std::move(group));
    }
    return std::nullopt;
}

std::optional<LoadError> parse_parameter_shapes(
    const Document& document,
    const Value& root,
    const std::vector<SlotData>& slots,
    const std::vector<SkinData>& skins,
    const std::unordered_set<std::string>& continuous_parameter_ids,
    ParameterModelDefinitions* model) {
    const Value* values = nullptr;
    if (const auto error = optional_root_array(document, root, "parameterShapes", &values)) {
        return error;
    }
    if (values == nullptr) {
        return std::nullopt;
    }
    const std::optional<std::size_t> default_skin = find_skin_index(skins, "default");
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values->as_array().size(); ++index) {
        const Value& value = values->as_array()[index];
        const std::string path = "$.parameterShapes[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, value, Value::Type::Object, path)) {
            return error;
        }
        ParameterShapeDefinition shape;
        if (const auto error = required_string(document, value, "id", path, &shape.id)) {
            return error;
        }
        if (!ids.insert(shape.id).second) {
            return parameter_error(document, value, path + ".id", "duplicate shape id");
        }
        if (const auto error = required_string(
                document, value, "target_slot", path, &shape.target_slot)) {
            return error;
        }
        const std::optional<std::size_t> slot_index = find_slot_index(slots, shape.target_slot);
        if (!slot_index.has_value()) {
            return parameter_error(
                document, value, path + ".target_slot", "shape references an unknown slot");
        }
        shape.target_slot_index = *slot_index;
        if (const auto error = required_string(
                document, value, "target_attachment", path, &shape.target_attachment)) {
            return error;
        }
        const AttachmentData* attachment = find_attachment_source_in_skins(
            skins, default_skin, *slot_index, shape.target_attachment, nullptr);
        if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
            return parameter_error(
                document,
                value,
                path + ".target_attachment",
                "shape target must resolve to a mesh attachment");
        }
        for (const SkinData& skin : skins) {
            for (const SkinSlotData& candidate : skin.slot_attachments) {
                if (candidate.slot_index != *slot_index ||
                    !attachment_matches_mesh_deform_source(
                        candidate.attachment,
                        shape.target_attachment)) {
                    continue;
                }
                if (candidate.attachment.mesh_geometry == nullptr ||
                    candidate.attachment.mesh_geometry->vertices.size() !=
                        attachment->mesh_geometry->vertices.size()) {
                    return parameter_error(
                        document,
                        value,
                        path + ".target_attachment",
                        "shape target and deform-inheriting linked meshes must share topology");
                }
            }
        }
        if (const auto error = required_string(
                document, value, "parameter", path, &shape.parameter)) {
            return error;
        }
        if (continuous_parameter_ids.find(shape.parameter) == continuous_parameter_ids.end()) {
            return parameter_error(
                document,
                value,
                path + ".parameter",
                "shape requires a known continuous parameter");
        }
        std::string blend_mode;
        if (const auto error = required_string(
                document, value, "blend_mode", path, &blend_mode)) {
            return error;
        }
        if (blend_mode == "additive_clamped") {
            shape.blend_mode = ParameterShapeBlendMode::AdditiveClamped;
        } else if (blend_mode == "normalized_override") {
            shape.blend_mode = ParameterShapeBlendMode::NormalizedOverride;
            for (const ParameterShapeDefinition& existing : model->parameter_shapes) {
                if (existing.blend_mode == ParameterShapeBlendMode::NormalizedOverride &&
                    existing.target_slot_index == shape.target_slot_index &&
                    shape_targets_overlap(
                        skins,
                        *slot_index,
                        existing.target_attachment,
                        shape.target_attachment)) {
                    return parameter_error(
                        document,
                        value,
                        path + ".blend_mode",
                        "only one normalized_override shape may match a mesh deform source");
                }
            }
        } else {
            return parameter_error(
                document,
                *json::find_member(value, "blend_mode"),
                path + ".blend_mode",
                "blend_mode must be 'additive_clamped' or 'normalized_override'");
        }

        const Value* keyforms = nullptr;
        if (const auto error = json::require_member(
                document, value, "keyforms", Value::Type::Array, path, &keyforms)) {
            return error;
        }
        if (keyforms->as_array().empty()) {
            return parameter_error(
                document, *keyforms, path + ".keyforms", "shape requires keyforms");
        }
        double previous = -std::numeric_limits<double>::infinity();
        for (std::size_t keyform_index = 0;
             keyform_index < keyforms->as_array().size();
             ++keyform_index) {
            const Value& keyform_value = keyforms->as_array()[keyform_index];
            const std::string keyform_path =
                path + ".keyforms[" + std::to_string(keyform_index) + "]";
            if (const auto error = json::require_type(
                    document, keyform_value, Value::Type::Object, keyform_path)) {
                return error;
            }
            ParameterShapeKeyform keyform;
            if (const auto error = required_number(
                    document, keyform_value, "value", keyform_path, &keyform.value)) {
                return error;
            }
            if (!(keyform.value > previous)) {
                return parameter_error(
                    document,
                    keyform_value,
                    keyform_path + ".value",
                    "shape keyform values must be strictly increasing");
            }
            previous = keyform.value;
            const Value* vertices = nullptr;
            if (const auto error = json::require_member(
                    document,
                    keyform_value,
                    "vertices",
                    Value::Type::Array,
                    keyform_path,
                    &vertices)) {
                return error;
            }
            if (const auto error = parse_number_array(
                    document, *vertices, keyform_path + ".vertices", &keyform.vertices)) {
                return error;
            }
            if (keyform.vertices.size() != attachment->mesh_geometry->vertices.size()) {
                return parameter_error(
                    document,
                    *vertices,
                    keyform_path + ".vertices",
                    "shape vertices must exactly match target mesh x/y component count");
            }
            shape.keyforms.push_back(std::move(keyform));
        }
        model->parameter_shapes.push_back(std::move(shape));
    }
    return std::nullopt;
}

std::optional<LoadError> parse_parameter_binding(
    const Document& document,
    const Value& value,
    std::string_view path,
    const std::unordered_set<std::string>& continuous_parameter_ids,
    ParameterBindingDefinition* output) {
    if (const auto error = json::require_type(document, value, Value::Type::Object, path)) {
        return error;
    }
    if (const auto error = required_string(
            document, value, "parameter", path, &output->parameter)) {
        return error;
    }
    if (continuous_parameter_ids.find(output->parameter) == continuous_parameter_ids.end()) {
        return parameter_error(
            document,
            value,
            std::string(path) + ".parameter",
            "deformer binding requires a known continuous parameter");
    }
    std::string axis;
    if (const auto error = required_string(document, value, "axis", path, &axis)) {
        return error;
    }
    if (axis == "x") {
        output->axis = ParameterDeformerAxis::X;
    } else if (axis == "y") {
        output->axis = ParameterDeformerAxis::Y;
    } else if (axis == "angle") {
        output->axis = ParameterDeformerAxis::Angle;
    } else {
        return parameter_error(
            document,
            *json::find_member(value, "axis"),
            std::string(path) + ".axis",
            "deformer axis must be 'x', 'y', or 'angle'");
    }
    return std::nullopt;
}

std::optional<LoadError> parse_parameter_deformers(
    const Document& document,
    const Value& root,
    const std::vector<SlotData>& slots,
    const std::vector<SkinData>& skins,
    const std::unordered_set<std::string>& continuous_parameter_ids,
    ParameterModelDefinitions* model) {
    const Value* values = nullptr;
    if (const auto error = optional_root_array(document, root, "parameterDeformers", &values)) {
        return error;
    }
    if (values == nullptr) {
        return std::nullopt;
    }
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values->as_array().size(); ++index) {
        const Value& value = values->as_array()[index];
        const std::string path = "$.parameterDeformers[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, value, Value::Type::Object, path)) {
            return error;
        }
        ParameterDeformerDefinition deformer;
        if (const auto error = required_string(document, value, "id", path, &deformer.id)) {
            return error;
        }
        if (!ids.insert(deformer.id).second) {
            return parameter_error(document, value, path + ".id", "duplicate deformer id");
        }
        if (const auto error = required_string(document, value, "name", path, &deformer.name)) {
            return error;
        }
        std::string kind;
        if (const auto error = required_string(document, value, "kind", path, &kind)) {
            return error;
        }
        if (kind == "warp") {
            deformer.kind = ParameterDeformerKind::Warp;
        } else if (kind == "rotation") {
            deformer.kind = ParameterDeformerKind::Rotation;
        } else {
            return parameter_error(
                document,
                *json::find_member(value, "kind"),
                path + ".kind",
                "deformer kind must be 'warp' or 'rotation'");
        }
        if (const auto error = optional_string(
                document, value, "parent", path, &deformer.parent)) {
            return error;
        }
        const Value* target_slots = nullptr;
        if (const auto error = json::require_member(
                document,
                value,
                "target_slots",
                Value::Type::Array,
                path,
                &target_slots)) {
            return error;
        }
        if (const auto error = parse_string_array(
                document, *target_slots, path + ".target_slots", &deformer.target_slots)) {
            return error;
        }
        for (const std::string& target_slot : deformer.target_slots) {
            const std::optional<std::size_t> slot_index =
                find_slot_index(slots, target_slot);
            if (!slot_index.has_value()) {
                return parameter_error(
                    document,
                    *target_slots,
                    path + ".target_slots",
                    "deformer references unknown slot '" + target_slot + "'");
            }
            if (!slot_has_mesh_attachment(skins, *slot_index)) {
                return parameter_error(
                    document,
                    *target_slots,
                    path + ".target_slots",
                    "deformer target slot '" + target_slot +
                        "' must contain a mesh attachment");
            }
        }

        const Value* bindings = nullptr;
        if (const auto error = json::require_member(
                document,
                value,
                "parameter_bindings",
                Value::Type::Array,
                path,
                &bindings)) {
            return error;
        }
        for (std::size_t binding_index = 0;
             binding_index < bindings->as_array().size();
             ++binding_index) {
            ParameterBindingDefinition binding;
            const std::string binding_path =
                path + ".parameter_bindings[" + std::to_string(binding_index) + "]";
            if (const auto error = parse_parameter_binding(
                    document,
                    bindings->as_array()[binding_index],
                    binding_path,
                    continuous_parameter_ids,
                    &binding)) {
                return error;
            }
            deformer.parameter_bindings.push_back(std::move(binding));
        }

        const Value* keyforms = nullptr;
        if (const auto error = json::require_member(
                document, value, "keyforms", Value::Type::Array, path, &keyforms)) {
            return error;
        }
        if (keyforms->as_array().empty()) {
            return parameter_error(
                document, *keyforms, path + ".keyforms", "deformer requires keyforms");
        }

        if (deformer.kind == ParameterDeformerKind::Warp) {
            if (deformer.parameter_bindings.size() != 2U ||
                deformer.parameter_bindings[0].parameter ==
                    deformer.parameter_bindings[1].parameter) {
                return parameter_error(
                    document,
                    *bindings,
                    path + ".parameter_bindings",
                    "warp requires distinct x and y parameter bindings");
            }
            bool has_x = false;
            bool has_y = false;
            for (const auto& binding : deformer.parameter_bindings) {
                has_x = has_x || binding.axis == ParameterDeformerAxis::X;
                has_y = has_y || binding.axis == ParameterDeformerAxis::Y;
            }
            if (!has_x || !has_y) {
                return parameter_error(
                    document,
                    *bindings,
                    path + ".parameter_bindings",
                    "warp bindings must contain exactly axes x and y");
            }
            if (const auto error = required_size(
                    document, value, "grid_cols", path, &deformer.grid_cols)) {
                return error;
            }
            if (const auto error = required_size(
                    document, value, "grid_rows", path, &deformer.grid_rows)) {
                return error;
            }
            if (deformer.grid_cols < 2U || deformer.grid_rows < 2U) {
                return parameter_error(
                    document,
                    value,
                    path,
                    "warp grid_cols and grid_rows must each be at least 2");
            }
            const Value* control_points = nullptr;
            if (const auto error = json::require_member(
                    document,
                    value,
                    "control_points",
                    Value::Type::Array,
                    path,
                    &control_points)) {
                return error;
            }
            if (const auto error = parse_points(
                    document,
                    *control_points,
                    path + ".control_points",
                    deformer.grid_cols * deformer.grid_rows,
                    &deformer.control_points)) {
                return error;
            }
            if (deformer.control_points.size() != deformer.grid_cols * deformer.grid_rows) {
                return parameter_error(
                    document,
                    *control_points,
                    path + ".control_points",
                    "warp control point count must equal grid_cols * grid_rows");
            }
            for (std::size_t row = 0; row < deformer.grid_rows; ++row) {
                for (std::size_t column = 0; column < deformer.grid_cols; ++column) {
                    const AttachmentVertex& point =
                        deformer.control_points[row * deformer.grid_cols + column];
                    if (!parameter_nearly_equal(
                            point.x,
                            deformer.control_points[column].x) ||
                        !parameter_nearly_equal(
                            point.y,
                            deformer.control_points[row * deformer.grid_cols].y)) {
                        return parameter_error(
                            document,
                            *control_points,
                            path + ".control_points",
                            "warp base lattice must be axis aligned and row-major");
                    }
                }
            }
            const auto strictly_monotonic = [](const std::vector<double>& coordinates) {
                if (coordinates.size() < 2U ||
                    parameter_nearly_equal(coordinates[0], coordinates[1])) {
                    return false;
                }
                const bool increasing = coordinates[1] > coordinates[0];
                for (std::size_t coordinate = 1; coordinate < coordinates.size(); ++coordinate) {
                    if (parameter_nearly_equal(
                            coordinates[coordinate - 1U],
                            coordinates[coordinate]) ||
                        (increasing
                             ? coordinates[coordinate] <= coordinates[coordinate - 1U]
                             : coordinates[coordinate] >= coordinates[coordinate - 1U])) {
                        return false;
                    }
                }
                return true;
            };
            std::vector<double> base_x(deformer.grid_cols);
            std::vector<double> base_y(deformer.grid_rows);
            for (std::size_t column = 0; column < deformer.grid_cols; ++column) {
                base_x[column] = deformer.control_points[column].x;
            }
            for (std::size_t row = 0; row < deformer.grid_rows; ++row) {
                base_y[row] = deformer.control_points[row * deformer.grid_cols].y;
            }
            if (!strictly_monotonic(base_x) || !strictly_monotonic(base_y)) {
                return parameter_error(
                    document,
                    *control_points,
                    path + ".control_points",
                    "warp base lattice axes must be strictly monotonic");
            }
            for (std::size_t keyform_index = 0;
                 keyform_index < keyforms->as_array().size();
                 ++keyform_index) {
                const Value& keyform_value = keyforms->as_array()[keyform_index];
                const std::string keyform_path =
                    path + ".keyforms[" + std::to_string(keyform_index) + "]";
                if (const auto error = json::require_type(
                        document, keyform_value, Value::Type::Object, keyform_path)) {
                    return error;
                }
                WarpDeformerKeyform keyform;
                if (const auto error = required_number(
                        document, keyform_value, "x", keyform_path, &keyform.x)) {
                    return error;
                }
                if (const auto error = required_number(
                        document, keyform_value, "y", keyform_path, &keyform.y)) {
                    return error;
                }
                const Value* points = nullptr;
                if (const auto error = json::require_member(
                        document,
                        keyform_value,
                        "control_points",
                        Value::Type::Array,
                        keyform_path,
                        &points)) {
                    return error;
                }
                if (const auto error = parse_points(
                        document,
                        *points,
                        keyform_path + ".control_points",
                        deformer.control_points.size(),
                        &keyform.control_points)) {
                    return error;
                }
                if (keyform.control_points.size() != deformer.control_points.size()) {
                    return parameter_error(
                        document,
                        *points,
                        keyform_path + ".control_points",
                        "warp keyform control point count must match the base lattice");
                }
                deformer.warp_keyforms.push_back(std::move(keyform));
            }
            std::vector<double> x_coordinates;
            std::vector<double> y_coordinates;
            for (const WarpDeformerKeyform& keyform : deformer.warp_keyforms) {
                if (std::none_of(
                        x_coordinates.begin(),
                        x_coordinates.end(),
                        [&](double coordinate) {
                            return parameter_nearly_equal(coordinate, keyform.x);
                        })) {
                    x_coordinates.push_back(keyform.x);
                }
                if (std::none_of(
                        y_coordinates.begin(),
                        y_coordinates.end(),
                        [&](double coordinate) {
                            return parameter_nearly_equal(coordinate, keyform.y);
                        })) {
                    y_coordinates.push_back(keyform.y);
                }
            }
            if (x_coordinates.size() < 2U || y_coordinates.size() < 2U ||
                deformer.warp_keyforms.size() !=
                    x_coordinates.size() * y_coordinates.size()) {
                return parameter_error(
                    document,
                    *keyforms,
                    path + ".keyforms",
                    "warp keyforms must form a complete x/y Cartesian grid");
            }
            for (double x : x_coordinates) {
                for (double y : y_coordinates) {
                    const std::size_t matches = static_cast<std::size_t>(std::count_if(
                        deformer.warp_keyforms.begin(),
                        deformer.warp_keyforms.end(),
                        [&](const WarpDeformerKeyform& keyform) {
                            return parameter_nearly_equal(keyform.x, x) &&
                                parameter_nearly_equal(keyform.y, y);
                        }));
                    if (matches != 1U) {
                        return parameter_error(
                            document,
                            *keyforms,
                            path + ".keyforms",
                            "warp keyforms must contain each x/y Cartesian pair exactly once");
                    }
                }
            }
        } else {
            if (deformer.parameter_bindings.size() != 1U ||
                deformer.parameter_bindings.front().axis != ParameterDeformerAxis::Angle) {
                return parameter_error(
                    document,
                    *bindings,
                    path + ".parameter_bindings",
                    "rotation requires exactly one angle parameter binding");
            }
            const Value* pivot = nullptr;
            if (const auto error = json::require_member(
                    document, value, "pivot", Value::Type::Array, path, &pivot)) {
                return error;
            }
            std::vector<AttachmentVertex> parsed_pivot;
            if (const auto error = parse_points(
                    document, *pivot, path + ".pivot", 1U, &parsed_pivot)) {
                return error;
            }
            if (parsed_pivot.size() != 1U) {
                return parameter_error(
                    document, *pivot, path + ".pivot", "rotation pivot must be one x/y pair");
            }
            deformer.pivot = parsed_pivot.front();
            if (const auto error = required_number(
                    document, value, "influence", path, &deformer.influence)) {
                return error;
            }
            if (deformer.influence < 0.0 || deformer.influence > 1.0) {
                return parameter_error(
                    document,
                    *json::find_member(value, "influence"),
                    path + ".influence",
                    "rotation influence must stay within [0, 1]");
            }
            double previous = -std::numeric_limits<double>::infinity();
            for (std::size_t keyform_index = 0;
                 keyform_index < keyforms->as_array().size();
                 ++keyform_index) {
                const Value& keyform_value = keyforms->as_array()[keyform_index];
                const std::string keyform_path =
                    path + ".keyforms[" + std::to_string(keyform_index) + "]";
                if (const auto error = json::require_type(
                        document, keyform_value, Value::Type::Object, keyform_path)) {
                    return error;
                }
                RotationDeformerKeyform keyform;
                if (const auto error = required_number(
                        document, keyform_value, "value", keyform_path, &keyform.value)) {
                    return error;
                }
                if (!(keyform.value > previous)) {
                    return parameter_error(
                        document,
                        keyform_value,
                        keyform_path + ".value",
                        "rotation keyform values must be strictly increasing");
                }
                previous = keyform.value;
                if (const auto error = required_number(
                        document, keyform_value, "angle", keyform_path, &keyform.angle)) {
                    return error;
                }
                deformer.rotation_keyforms.push_back(keyform);
            }
        }
        model->parameter_deformers.push_back(std::move(deformer));
    }

    std::unordered_set<std::string> deformer_ids;
    for (const ParameterDeformerDefinition& deformer : model->parameter_deformers) {
        deformer_ids.insert(deformer.id);
    }
    std::unordered_set<std::size_t> targeted_slots;
    for (std::size_t index = 0; index < model->parameter_deformers.size(); ++index) {
        const ParameterDeformerDefinition& deformer = model->parameter_deformers[index];
        const Value& value = values->as_array()[index];
        const std::string path = "$.parameterDeformers[" + std::to_string(index) + "]";
        if (deformer.parent.has_value()) {
            if (*deformer.parent == deformer.id ||
                deformer_ids.find(*deformer.parent) == deformer_ids.end()) {
                return parameter_error(
                    document,
                    value,
                    path + ".parent",
                    "deformer parent must reference a different existing deformer");
            }
            const auto parent = std::find_if(
                model->parameter_deformers.begin(),
                model->parameter_deformers.end(),
                [&](const ParameterDeformerDefinition& candidate) {
                    return candidate.id == *deformer.parent;
                });
            if (parent != model->parameter_deformers.end() && parent->parent.has_value()) {
                return parameter_error(
                    document,
                    value,
                    path + ".parent",
                    "deformer nesting is limited to one parent level");
            }
        }
        for (const std::string& slot_name : deformer.target_slots) {
            const std::size_t slot_index = *find_slot_index(slots, slot_name);
            if (!targeted_slots.insert(slot_index).second) {
                return parameter_error(
                    document,
                    value,
                    path + ".target_slots",
                    "a slot may be targeted by only one deformer leaf chain");
            }
        }
    }
    return std::nullopt;
}

std::optional<LoadError> parse_art_paths(
    const Document& document,
    const Value& root,
    const std::unordered_set<std::string>& continuous_parameter_ids,
    const std::unordered_set<std::string>& deformer_ids,
    ParameterModelDefinitions* model) {
    const Value* values = nullptr;
    if (const auto error = optional_root_array(document, root, "artPaths", &values)) {
        return error;
    }
    if (values == nullptr) {
        return std::nullopt;
    }
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values->as_array().size(); ++index) {
        const Value& value = values->as_array()[index];
        const std::string path = "$.artPaths[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, value, Value::Type::Object, path)) {
            return error;
        }
        ArtPathDefinition art_path;
        if (const auto error = required_string(document, value, "id", path, &art_path.id)) {
            return error;
        }
        if (!ids.insert(art_path.id).second) {
            return parameter_error(document, value, path + ".id", "duplicate art path id");
        }
        if (const auto error = required_string(document, value, "name", path, &art_path.name)) {
            return error;
        }
        if (const auto error = optional_string(
                document,
                value,
                "parent_deformer",
                path,
                &art_path.parent_deformer)) {
            return error;
        }
        if (art_path.parent_deformer.has_value() &&
            deformer_ids.find(*art_path.parent_deformer) == deformer_ids.end()) {
            return parameter_error(
                document,
                value,
                path + ".parent_deformer",
                "art path references unknown parent deformer");
        }
        const Value* points = nullptr;
        if (const auto error = json::require_member(
                document, value, "points", Value::Type::Array, path, &points)) {
            return error;
        }
        if (const auto error = parse_points(
                document, *points, path + ".points", 2U, &art_path.points)) {
            return error;
        }
        if (!has_effective_path_segment(art_path.points)) {
            return parameter_error(
                document,
                *points,
                path + ".points",
                "art path requires at least one non-zero-length segment");
        }
        if (const auto error = required_number(
                document, value, "width", path, &art_path.width)) {
            return error;
        }
        if (art_path.width <= 0.0) {
            return parameter_error(
                document, value, path + ".width", "art path width must be positive");
        }
        const Value* color = nullptr;
        if (const auto error = json::require_member(
                document, value, "color", Value::Type::Object, path, &color)) {
            return error;
        }
        if (const auto error = parse_color(
                document, *color, path + ".color", &art_path.color)) {
            return error;
        }
        std::string cap;
        if (const auto error = required_string(document, value, "cap", path, &cap)) {
            return error;
        }
        if (cap == "butt") {
            art_path.cap = ArtPathCap::Butt;
        } else if (cap == "square") {
            art_path.cap = ArtPathCap::Square;
        } else if (cap == "round") {
            art_path.cap = ArtPathCap::Round;
        } else {
            return parameter_error(
                document,
                *json::find_member(value, "cap"),
                path + ".cap",
                "art path cap must be 'butt', 'square', or 'round'");
        }
        std::string join;
        if (const auto error = required_string(document, value, "join", path, &join)) {
            return error;
        }
        if (join == "miter") {
            art_path.join = ArtPathJoin::Miter;
        } else if (join == "bevel") {
            art_path.join = ArtPathJoin::Bevel;
        } else if (join == "round") {
            art_path.join = ArtPathJoin::Round;
        } else {
            return parameter_error(
                document,
                *json::find_member(value, "join"),
                path + ".join",
                "art path join must be 'miter', 'bevel', or 'round'");
        }

        if (const Value* parameter_keyforms = optional_member(value, "parameter_keyforms")) {
            const std::string parameter_path = path + ".parameter_keyforms";
            if (const auto error = json::require_type(
                    document,
                    *parameter_keyforms,
                    Value::Type::Object,
                    parameter_path)) {
                return error;
            }
            ArtPathParameterKeyforms parsed;
            if (const auto error = required_string(
                    document,
                    *parameter_keyforms,
                    "parameter",
                    parameter_path,
                    &parsed.parameter)) {
                return error;
            }
            if (continuous_parameter_ids.find(parsed.parameter) ==
                continuous_parameter_ids.end()) {
                return parameter_error(
                    document,
                    *parameter_keyforms,
                    parameter_path + ".parameter",
                    "art path keyforms require a known continuous parameter");
            }
            const Value* keyforms = nullptr;
            if (const auto error = json::require_member(
                    document,
                    *parameter_keyforms,
                    "keyforms",
                    Value::Type::Array,
                    parameter_path,
                    &keyforms)) {
                return error;
            }
            if (keyforms->as_array().empty()) {
                return parameter_error(
                    document,
                    *keyforms,
                    parameter_path + ".keyforms",
                    "art path parameter_keyforms must not be empty");
            }
            double previous = -std::numeric_limits<double>::infinity();
            for (std::size_t keyform_index = 0;
                 keyform_index < keyforms->as_array().size();
                 ++keyform_index) {
                const Value& keyform_value = keyforms->as_array()[keyform_index];
                const std::string keyform_path =
                    parameter_path + ".keyforms[" + std::to_string(keyform_index) + "]";
                if (const auto error = json::require_type(
                        document, keyform_value, Value::Type::Object, keyform_path)) {
                    return error;
                }
                ArtPathKeyform keyform;
                if (const auto error = required_number(
                        document, keyform_value, "value", keyform_path, &keyform.value)) {
                    return error;
                }
                if (!(keyform.value > previous)) {
                    return parameter_error(
                        document,
                        keyform_value,
                        keyform_path + ".value",
                        "art path keyform values must be strictly increasing");
                }
                previous = keyform.value;
                const Value* keyform_points = nullptr;
                if (const auto error = json::require_member(
                        document,
                        keyform_value,
                        "points",
                        Value::Type::Array,
                        keyform_path,
                        &keyform_points)) {
                    return error;
                }
                if (const auto error = parse_points(
                        document,
                        *keyform_points,
                        keyform_path + ".points",
                        2U,
                        &keyform.points)) {
                    return error;
                }
                if (keyform.points.size() != art_path.points.size()) {
                    return parameter_error(
                        document,
                        *keyform_points,
                        keyform_path + ".points",
                        "art path keyform point count must match the base path");
                }
                if (!has_effective_path_segment(keyform.points)) {
                    return parameter_error(
                        document,
                        *keyform_points,
                        keyform_path + ".points",
                        "art path keyform requires at least one non-zero-length segment");
                }
                if (const auto error = required_number(
                        document, keyform_value, "width", keyform_path, &keyform.width)) {
                    return error;
                }
                if (keyform.width <= 0.0) {
                    return parameter_error(
                        document,
                        keyform_value,
                        keyform_path + ".width",
                        "art path keyform width must be positive");
                }
                const Value* keyform_color = nullptr;
                if (const auto error = json::require_member(
                        document,
                        keyform_value,
                        "color",
                        Value::Type::Object,
                        keyform_path,
                        &keyform_color)) {
                    return error;
                }
                if (const auto error = parse_color(
                        document,
                        *keyform_color,
                        keyform_path + ".color",
                        &keyform.color)) {
                    return error;
                }
                parsed.keyforms.push_back(std::move(keyform));
            }
            art_path.parameter_keyforms = std::move(parsed);
        }
        model->art_paths.push_back(std::move(art_path));
    }
    return std::nullopt;
}

std::optional<LoadError> parse_expressions(
    const Document& document,
    const Value& root,
    const std::unordered_set<std::string>& parameter_ids,
    ParameterModelDefinitions* model) {
    const Value* values = nullptr;
    if (const auto error = optional_root_array(document, root, "expressions", &values)) {
        return error;
    }
    if (values == nullptr) {
        return std::nullopt;
    }
    std::unordered_set<std::string> ids;
    for (std::size_t index = 0; index < values->as_array().size(); ++index) {
        const Value& value = values->as_array()[index];
        const std::string path = "$.expressions[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, value, Value::Type::Object, path)) {
            return error;
        }
        ExpressionDefinition expression;
        if (const auto error = required_string(document, value, "id", path, &expression.id)) {
            return error;
        }
        if (!ids.insert(expression.id).second) {
            return parameter_error(document, value, path + ".id", "duplicate expression id");
        }
        if (const auto error = required_string(document, value, "name", path, &expression.name)) {
            return error;
        }
        const Value* targets = nullptr;
        if (const auto error = json::require_member(
                document, value, "targets", Value::Type::Array, path, &targets)) {
            return error;
        }
        if (targets->as_array().empty()) {
            return parameter_error(
                document, *targets, path + ".targets", "expression requires targets");
        }
        std::unordered_set<std::string> target_parameters;
        for (std::size_t target_index = 0;
             target_index < targets->as_array().size();
             ++target_index) {
            const Value& target_value = targets->as_array()[target_index];
            const std::string target_path =
                path + ".targets[" + std::to_string(target_index) + "]";
            if (const auto error = json::require_type(
                    document, target_value, Value::Type::Object, target_path)) {
                return error;
            }
            ExpressionTargetDefinition target;
            if (const auto error = required_string(
                    document, target_value, "parameter", target_path, &target.parameter)) {
                return error;
            }
            if (parameter_ids.find(target.parameter) == parameter_ids.end()) {
                return parameter_error(
                    document,
                    target_value,
                    target_path + ".parameter",
                    "expression references unknown parameter");
            }
            if (!target_parameters.insert(target.parameter).second) {
                return parameter_error(
                    document,
                    target_value,
                    target_path + ".parameter",
                    "expression repeats a parameter target");
            }
            if (const auto error = required_number(
                    document, target_value, "value", target_path, &target.value)) {
                return error;
            }
            expression.targets.push_back(std::move(target));
        }
        if (const auto error = required_number(
                document, value, "duration", path, &expression.duration)) {
            return error;
        }
        if (expression.duration < 0.0) {
            return parameter_error(
                document, value, path + ".duration", "expression duration must be non-negative");
        }
        std::string blend;
        if (const auto error = required_string(document, value, "blend", path, &blend)) {
            return error;
        }
        if (blend == "additive") {
            expression.blend = ExpressionBlend::Additive;
        } else if (blend == "override") {
            expression.blend = ExpressionBlend::Override;
        } else {
            return parameter_error(
                document,
                *json::find_member(value, "blend"),
                path + ".blend",
                "expression blend must be 'additive' or 'override'");
        }
        if (const auto error = required_int(
                document, value, "priority", path, &expression.priority)) {
            return error;
        }
        std::string reset_policy;
        if (const auto error = required_string(
                document, value, "reset_policy", path, &reset_policy)) {
            return error;
        }
        if (reset_policy == "restore") {
            expression.reset_policy = ExpressionResetPolicy::Restore;
        } else if (reset_policy == "hold") {
            expression.reset_policy = ExpressionResetPolicy::Hold;
        } else {
            return parameter_error(
                document,
                *json::find_member(value, "reset_policy"),
                path + ".reset_policy",
                "expression reset_policy must be 'restore' or 'hold'");
        }
        model->expressions.push_back(std::move(expression));
    }
    return std::nullopt;
}

std::optional<LoadError> parse_lip_sync(
    const Document& document,
    const Value& root,
    const std::unordered_set<std::string>& parameter_ids,
    ParameterModelDefinitions* model) {
    const Value* lip_sync = json::find_member(root, "lipSync");
    if (lip_sync == nullptr) {
        return std::nullopt;
    }
    if (const auto error = json::require_type(
            document, *lip_sync, Value::Type::Object, "$.lipSync")) {
        return error;
    }
    const Value* mappings = nullptr;
    if (const auto error = json::require_member(
            document,
            *lip_sync,
            "mappings",
            Value::Type::Array,
            "$.lipSync",
            &mappings)) {
        return error;
    }
    std::unordered_set<std::string> targets;
    for (std::size_t index = 0; index < mappings->as_array().size(); ++index) {
        const Value& value = mappings->as_array()[index];
        const std::string path = "$.lipSync.mappings[" + std::to_string(index) + "]";
        if (const auto error = json::require_type(
                document, value, Value::Type::Object, path)) {
            return error;
        }
        LipSyncMappingDefinition mapping;
        std::string source;
        if (const auto error = required_string(document, value, "source", path, &source)) {
            return error;
        }
        if (source == "amplitude") {
            mapping.source = LipSyncSource::Amplitude;
        } else if (source == "phoneme") {
            mapping.source = LipSyncSource::Phoneme;
        } else {
            return parameter_error(
                document,
                *json::find_member(value, "source"),
                path + ".source",
                "lip-sync source must be 'amplitude' or 'phoneme'");
        }
        if (const auto error = required_string(
                document, value, "parameter", path, &mapping.parameter)) {
            return error;
        }
        if (parameter_ids.find(mapping.parameter) == parameter_ids.end()) {
            return parameter_error(
                document,
                value,
                path + ".parameter",
                "lip-sync mapping references unknown parameter");
        }
        if (!targets.insert(mapping.parameter).second) {
            return parameter_error(
                document,
                value,
                path + ".parameter",
                "lip-sync mappings must have unique target parameters");
        }
        if (const auto error = optional_number(
                document, value, "scale", path, &mapping.scale)) {
            return error;
        }
        if (const auto error = optional_number(
                document, value, "bias", path, &mapping.bias)) {
            return error;
        }
        if (const auto error = optional_number(
                document, value, "smoothing", path, &mapping.smoothing)) {
            return error;
        }
        if (const auto error = optional_number(
                document, value, "attack", path, &mapping.attack)) {
            return error;
        }
        if (const auto error = optional_number(
                document, value, "release", path, &mapping.release)) {
            return error;
        }
        if (mapping.smoothing < 0.0 || mapping.attack < 0.0 || mapping.release < 0.0) {
            return parameter_error(
                document,
                value,
                path,
                "lip-sync smoothing, attack, and release must be non-negative");
        }
        if (const Value* phoneme_map = optional_member(value, "phoneme_map")) {
            if (const auto error = json::require_type(
                    document,
                    *phoneme_map,
                    Value::Type::Object,
                    path + ".phoneme_map")) {
                return error;
            }
            for (const auto& [phoneme, phoneme_value] : phoneme_map->as_object()) {
                if (phoneme.empty()) {
                    return parameter_error(
                        document,
                        phoneme_value,
                        path + ".phoneme_map",
                        "phoneme keys must not be empty");
                }
                if (const auto error = json::require_type(
                        document,
                        phoneme_value,
                        Value::Type::Number,
                        path + ".phoneme_map." + phoneme)) {
                    return error;
                }
                if (!std::isfinite(phoneme_value.as_number())) {
                    return parameter_error(
                        document,
                        phoneme_value,
                        path + ".phoneme_map." + phoneme,
                        "phoneme value must be finite");
                }
                mapping.phoneme_map.push_back({phoneme, phoneme_value.as_number()});
            }
        }
        model->lip_sync.mappings.push_back(std::move(mapping));
    }
    return std::nullopt;
}

} // namespace

std::optional<json::LoadError> parse_parameter_model(
    const json::Document& document,
    const json::Value& root,
    const std::vector<SlotData>& slots,
    const std::vector<SkinData>& skins,
    ParameterModelDefinitions* model_out) {
    *model_out = {};
    if (const auto error = parse_parameters(document, root, model_out)) {
        return error;
    }

    std::unordered_set<std::string> parameter_ids;
    std::unordered_set<std::string> continuous_parameter_ids;
    for (const ParameterDefinition& parameter : model_out->parameters) {
        parameter_ids.insert(parameter.id);
        if (parameter.type == ParameterType::Continuous) {
            continuous_parameter_ids.insert(parameter.id);
        }
    }
    if (const auto error = parse_parameter_groups(
            document, root, parameter_ids, model_out)) {
        return error;
    }
    if (const auto error = parse_parameter_shapes(
            document,
            root,
            slots,
            skins,
            continuous_parameter_ids,
            model_out)) {
        return error;
    }
    if (const auto error = parse_parameter_deformers(
            document,
            root,
            slots,
            skins,
            continuous_parameter_ids,
            model_out)) {
        return error;
    }
    std::unordered_set<std::string> deformer_ids;
    for (const ParameterDeformerDefinition& deformer : model_out->parameter_deformers) {
        deformer_ids.insert(deformer.id);
    }
    if (const auto error = parse_art_paths(
            document,
            root,
            continuous_parameter_ids,
            deformer_ids,
            model_out)) {
        return error;
    }
    if (const auto error = parse_expressions(
            document, root, parameter_ids, model_out)) {
        return error;
    }
    if (const auto error = parse_lip_sync(
            document, root, parameter_ids, model_out)) {
        return error;
    }
    return std::nullopt;
}

} // namespace marrow::runtime::detail
