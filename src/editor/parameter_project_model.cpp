#include "marrow/editor/project.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace marrow::editor {
namespace {

using runtime::json::Value;

bool fail(std::string* error_out, std::string message) {
    if (error_out != nullptr) *error_out = std::move(message);
    return false;
}

const Value* member(const Value& object, std::string_view key) {
    return object.is_object() ? runtime::json::find_member(object, key) : nullptr;
}

bool require_object(const Value& value, std::string_view label, std::string* error_out) {
    return value.is_object() ||
        fail(error_out, std::string(label) + " must be an object");
}

bool read_string(
    const Value& object,
    std::string_view key,
    std::string* output,
    std::string* error_out) {
    const Value* value = member(object, key);
    if (value == nullptr || !value->is_string()) {
        return fail(error_out, std::string(key) + " must be a string");
    }
    *output = value->as_string();
    return true;
}

bool read_optional_string(
    const Value& object,
    std::string_view key,
    std::optional<std::string>* output,
    std::string* error_out) {
    const Value* value = member(object, key);
    if (value == nullptr) {
        output->reset();
        return true;
    }
    if (!value->is_string()) {
        return fail(error_out, std::string(key) + " must be a string");
    }
    *output = value->as_string();
    return true;
}

bool read_number(
    const Value& object,
    std::string_view key,
    double* output,
    std::string* error_out) {
    const Value* value = member(object, key);
    if (value == nullptr || !value->is_number() || !std::isfinite(value->as_number())) {
        return fail(error_out, std::string(key) + " must be a finite number");
    }
    *output = value->as_number();
    return true;
}

bool read_optional_number(
    const Value& object,
    std::string_view key,
    double default_value,
    double* output,
    std::string* error_out) {
    const Value* value = member(object, key);
    if (value == nullptr) {
        *output = default_value;
        return true;
    }
    if (!value->is_number() || !std::isfinite(value->as_number())) {
        return fail(error_out, std::string(key) + " must be a finite number");
    }
    *output = value->as_number();
    return true;
}

bool read_size(
    const Value& object,
    std::string_view key,
    std::size_t* output,
    std::string* error_out) {
    double value = 0.0;
    if (!read_number(object, key, &value, error_out) || value < 0.0 ||
        std::floor(value) != value ||
        value > static_cast<double>(std::numeric_limits<std::size_t>::max())) {
        if (error_out != nullptr && error_out->empty()) {
            *error_out = std::string(key) + " must be a non-negative integer";
        }
        return false;
    }
    *output = static_cast<std::size_t>(value);
    return true;
}

bool read_int(
    const Value& object,
    std::string_view key,
    int* output,
    std::string* error_out) {
    double value = 0.0;
    if (!read_number(object, key, &value, error_out) || std::floor(value) != value ||
        value < static_cast<double>(std::numeric_limits<int>::min()) ||
        value > static_cast<double>(std::numeric_limits<int>::max())) {
        if (error_out != nullptr && error_out->empty()) {
            *error_out = std::string(key) + " must be an integer";
        }
        return false;
    }
    *output = static_cast<int>(value);
    return true;
}

bool read_number_array(
    const Value& object,
    std::string_view key,
    std::vector<double>* output,
    std::string* error_out) {
    const Value* values = member(object, key);
    if (values == nullptr || !values->is_array()) {
        return fail(error_out, std::string(key) + " must be an array");
    }
    output->clear();
    output->reserve(values->as_array().size());
    for (const Value& value : values->as_array()) {
        if (!value.is_number() || !std::isfinite(value.as_number())) {
            return fail(error_out, std::string(key) + " must contain finite numbers");
        }
        output->push_back(value.as_number());
    }
    return true;
}

bool read_points(
    const Value& object,
    std::string_view key,
    std::vector<runtime::AttachmentVertex>* output,
    std::string* error_out) {
    std::vector<double> values;
    if (!read_number_array(object, key, &values, error_out)) return false;
    if (values.size() % 2U != 0U) {
        return fail(error_out, std::string(key) + " must contain x/y pairs");
    }
    output->clear();
    output->reserve(values.size() / 2U);
    for (std::size_t index = 0U; index < values.size(); index += 2U) {
        output->emplace_back(values[index], values[index + 1U]);
    }
    return true;
}

bool read_string_array(
    const Value& object,
    std::string_view key,
    std::vector<std::string>* output,
    std::string* error_out) {
    const Value* values = member(object, key);
    if (values == nullptr || !values->is_array()) {
        return fail(error_out, std::string(key) + " must be an array");
    }
    output->clear();
    output->reserve(values->as_array().size());
    for (const Value& value : values->as_array()) {
        if (!value.is_string()) {
            return fail(error_out, std::string(key) + " must contain strings");
        }
        output->push_back(value.as_string());
    }
    return true;
}

bool read_color(
    const Value& object,
    std::string_view key,
    runtime::SlotColor* output,
    std::string* error_out) {
    const Value* color = member(object, key);
    if (color == nullptr || !color->is_object()) {
        return fail(error_out, std::string(key) + " must be an object");
    }
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
    double a = 1.0;
    if (!read_number(*color, "r", &r, error_out) ||
        !read_number(*color, "g", &g, error_out) ||
        !read_number(*color, "b", &b, error_out) ||
        !read_number(*color, "a", &a, error_out)) {
        return false;
    }
    *output = runtime::SlotColor{r, g, b, a};
    return true;
}

Value string_value(std::string value) { return Value(std::move(value), {}); }
Value number_value(double value) { return Value(value, {}); }
Value object_value(Value::Object value = {}) { return Value(std::move(value), {}); }
Value array_value(Value::Array value = {}) { return Value(std::move(value), {}); }

Value::Object source_object(const Value& source) {
    return source.is_object() ? source.as_object() : Value::Object{};
}

Value number_array(const std::vector<double>& values) {
    Value::Array output;
    output.reserve(values.size());
    for (double value : values) output.push_back(number_value(value));
    return array_value(std::move(output));
}

Value point_array(const std::vector<runtime::AttachmentVertex>& points) {
    Value::Array output;
    output.reserve(points.size() * 2U);
    for (const runtime::AttachmentVertex& point : points) {
        output.push_back(number_value(point.x));
        output.push_back(number_value(point.y));
    }
    return array_value(std::move(output));
}

Value string_array(const std::vector<std::string>& strings) {
    Value::Array output;
    output.reserve(strings.size());
    for (const std::string& value : strings) output.push_back(string_value(value));
    return array_value(std::move(output));
}

template <typename Predicate>
const Value* source_array_match(
    const Value& source,
    std::string_view key,
    const Predicate& predicate) {
    const Value* values = member(source, key);
    if (values == nullptr || !values->is_array()) return nullptr;
    const auto found = std::find_if(
        values->as_array().begin(), values->as_array().end(), predicate);
    return found == values->as_array().end() ? nullptr : &*found;
}

bool same_number_member(const Value& value, std::string_view key, double expected) {
    const Value* candidate = member(value, key);
    return candidate != nullptr && candidate->is_number() &&
        candidate->as_number() == expected;
}

Value color_value(const runtime::SlotColor& color, const Value* source = nullptr) {
    Value::Object object = source != nullptr ? source_object(*source) : Value::Object{};
    object["r"] = number_value(color.r);
    object["g"] = number_value(color.g);
    object["b"] = number_value(color.b);
    object["a"] = number_value(color.a);
    return object_value(std::move(object));
}

const char* axis_name(runtime::ParameterDeformerAxis axis) {
    switch (axis) {
        case runtime::ParameterDeformerAxis::X: return "x";
        case runtime::ParameterDeformerAxis::Y: return "y";
        case runtime::ParameterDeformerAxis::Angle: return "angle";
    }
    return "x";
}

} // namespace

bool LipSyncAuthoringDefinition::empty() const noexcept {
    if (!mappings.empty()) return false;
    if (!preserved_source.is_object()) return true;
    for (const auto& [key, unused] : preserved_source.as_object()) {
        (void)unused;
        if (key != "mappings") return false;
    }
    return true;
}

bool parse_parameter_shape_authoring_value(
    const Value& value,
    ParameterShapeAuthoringDefinition* definition_out,
    std::string* error_out) {
    if (definition_out == nullptr || !require_object(value, "parameter shape", error_out)) {
        return false;
    }
    ParameterShapeAuthoringDefinition definition;
    definition.preserved_source = value;
    std::string blend_mode;
    if (!read_string(value, "id", &definition.id, error_out) ||
        !read_string(value, "target_slot", &definition.target_slot, error_out) ||
        !read_string(value, "target_attachment", &definition.target_attachment, error_out) ||
        !read_string(value, "parameter", &definition.parameter, error_out) ||
        !read_string(value, "blend_mode", &blend_mode, error_out)) {
        return false;
    }
    if (blend_mode == "additive_clamped") {
        definition.blend_mode = runtime::ParameterShapeBlendMode::AdditiveClamped;
    } else if (blend_mode == "normalized_override") {
        definition.blend_mode = runtime::ParameterShapeBlendMode::NormalizedOverride;
    } else {
        return fail(error_out, "blend_mode must be additive_clamped or normalized_override");
    }
    const Value* keyforms = member(value, "keyforms");
    if (keyforms == nullptr || !keyforms->is_array()) {
        return fail(error_out, "keyforms must be an array");
    }
    for (const Value& keyform_value : keyforms->as_array()) {
        if (!keyform_value.is_object()) return fail(error_out, "shape keyforms must be objects");
        runtime::ParameterShapeKeyform keyform;
        if (!read_number(keyform_value, "value", &keyform.value, error_out) ||
            !read_number_array(keyform_value, "vertices", &keyform.vertices, error_out)) {
            return false;
        }
        definition.keyforms.push_back(std::move(keyform));
    }
    *definition_out = std::move(definition);
    return true;
}

Value build_parameter_shape_authoring_value(
    const ParameterShapeAuthoringDefinition& definition) {
    Value::Object object = source_object(definition.preserved_source);
    object["id"] = string_value(definition.id);
    object["target_slot"] = string_value(definition.target_slot);
    object["target_attachment"] = string_value(definition.target_attachment);
    object["parameter"] = string_value(definition.parameter);
    object["blend_mode"] = string_value(
        definition.blend_mode == runtime::ParameterShapeBlendMode::NormalizedOverride
            ? "normalized_override"
            : "additive_clamped");
    Value::Array keyforms;
    keyforms.reserve(definition.keyforms.size());
    for (const runtime::ParameterShapeKeyform& keyform : definition.keyforms) {
        const Value* source = source_array_match(
            definition.preserved_source, "keyforms", [&](const Value& candidate) {
                return same_number_member(candidate, "value", keyform.value);
            });
        Value::Object keyform_object = source != nullptr
            ? source_object(*source)
            : Value::Object{};
        keyform_object["value"] = number_value(keyform.value);
        keyform_object["vertices"] = number_array(keyform.vertices);
        keyforms.push_back(object_value(std::move(keyform_object)));
    }
    object["keyforms"] = array_value(std::move(keyforms));
    return object_value(std::move(object));
}

bool parse_parameter_deformer_authoring_value(
    const Value& value,
    ParameterDeformerAuthoringDefinition* definition_out,
    std::string* error_out) {
    if (definition_out == nullptr || !require_object(value, "parameter deformer", error_out)) {
        return false;
    }
    ParameterDeformerAuthoringDefinition definition;
    definition.preserved_source = value;
    std::string kind;
    if (!read_string(value, "id", &definition.id, error_out) ||
        !read_string(value, "name", &definition.name, error_out) ||
        !read_string(value, "kind", &kind, error_out) ||
        !read_optional_string(value, "parent", &definition.parent, error_out) ||
        !read_string_array(value, "target_slots", &definition.target_slots, error_out)) {
        return false;
    }
    if (kind == "warp") {
        definition.kind = runtime::ParameterDeformerKind::Warp;
    } else if (kind == "rotation") {
        definition.kind = runtime::ParameterDeformerKind::Rotation;
    } else {
        return fail(error_out, "kind must be warp or rotation");
    }
    const Value* bindings = member(value, "parameter_bindings");
    if (bindings == nullptr || !bindings->is_array()) {
        return fail(error_out, "parameter_bindings must be an array");
    }
    for (const Value& binding_value : bindings->as_array()) {
        if (!binding_value.is_object()) {
            return fail(error_out, "parameter bindings must be objects");
        }
        runtime::ParameterBindingDefinition binding;
        std::string axis;
        if (!read_string(binding_value, "parameter", &binding.parameter, error_out) ||
            !read_string(binding_value, "axis", &axis, error_out)) {
            return false;
        }
        if (axis == "x") binding.axis = runtime::ParameterDeformerAxis::X;
        else if (axis == "y") binding.axis = runtime::ParameterDeformerAxis::Y;
        else if (axis == "angle") binding.axis = runtime::ParameterDeformerAxis::Angle;
        else return fail(error_out, "binding axis must be x, y, or angle");
        definition.parameter_bindings.push_back(std::move(binding));
    }
    const Value* keyforms = member(value, "keyforms");
    if (keyforms == nullptr || !keyforms->is_array()) {
        return fail(error_out, "keyforms must be an array");
    }
    if (definition.kind == runtime::ParameterDeformerKind::Warp) {
        if (!read_size(value, "grid_cols", &definition.grid_cols, error_out) ||
            !read_size(value, "grid_rows", &definition.grid_rows, error_out) ||
            !read_points(value, "control_points", &definition.control_points, error_out)) {
            return false;
        }
        for (const Value& keyform_value : keyforms->as_array()) {
            if (!keyform_value.is_object()) {
                return fail(error_out, "warp keyforms must be objects");
            }
            runtime::WarpDeformerKeyform keyform;
            if (!read_number(keyform_value, "x", &keyform.x, error_out) ||
                !read_number(keyform_value, "y", &keyform.y, error_out) ||
                !read_points(
                    keyform_value, "control_points", &keyform.control_points, error_out)) {
                return false;
            }
            definition.warp_keyforms.push_back(std::move(keyform));
        }
    } else {
        std::vector<runtime::AttachmentVertex> pivot;
        if (!read_points(value, "pivot", &pivot, error_out)) return false;
        if (pivot.size() != 1U) {
            return fail(error_out, "pivot must contain one x/y pair");
        }
        if (!read_number(value, "influence", &definition.influence, error_out)) return false;
        definition.pivot = pivot.front();
        for (const Value& keyform_value : keyforms->as_array()) {
            if (!keyform_value.is_object()) {
                return fail(error_out, "rotation keyforms must be objects");
            }
            runtime::RotationDeformerKeyform keyform;
            if (!read_number(keyform_value, "value", &keyform.value, error_out) ||
                !read_number(keyform_value, "angle", &keyform.angle, error_out)) {
                return false;
            }
            definition.rotation_keyforms.push_back(keyform);
        }
    }
    *definition_out = std::move(definition);
    return true;
}

Value build_parameter_deformer_authoring_value(
    const ParameterDeformerAuthoringDefinition& definition) {
    Value::Object object = source_object(definition.preserved_source);
    object["id"] = string_value(definition.id);
    object["name"] = string_value(definition.name);
    object["kind"] = string_value(
        definition.kind == runtime::ParameterDeformerKind::Warp ? "warp" : "rotation");
    if (definition.parent.has_value()) object["parent"] = string_value(*definition.parent);
    else object.erase("parent");
    object["target_slots"] = string_array(definition.target_slots);

    Value::Array bindings;
    bindings.reserve(definition.parameter_bindings.size());
    for (const runtime::ParameterBindingDefinition& binding : definition.parameter_bindings) {
        const Value* source = source_array_match(
            definition.preserved_source, "parameter_bindings", [&](const Value& candidate) {
                const Value* parameter = member(candidate, "parameter");
                const Value* axis = member(candidate, "axis");
                return parameter != nullptr && parameter->is_string() &&
                    parameter->as_string() == binding.parameter && axis != nullptr &&
                    axis->is_string() && axis->as_string() == axis_name(binding.axis);
            });
        Value::Object binding_object = source != nullptr
            ? source_object(*source)
            : Value::Object{};
        binding_object["parameter"] = string_value(binding.parameter);
        binding_object["axis"] = string_value(axis_name(binding.axis));
        bindings.push_back(object_value(std::move(binding_object)));
    }
    object["parameter_bindings"] = array_value(std::move(bindings));

    Value::Array keyforms;
    if (definition.kind == runtime::ParameterDeformerKind::Warp) {
        object.erase("pivot");
        object.erase("influence");
        object["grid_cols"] = number_value(static_cast<double>(definition.grid_cols));
        object["grid_rows"] = number_value(static_cast<double>(definition.grid_rows));
        object["control_points"] = point_array(definition.control_points);
        keyforms.reserve(definition.warp_keyforms.size());
        for (const runtime::WarpDeformerKeyform& keyform : definition.warp_keyforms) {
            const Value* source = source_array_match(
                definition.preserved_source, "keyforms", [&](const Value& candidate) {
                    return same_number_member(candidate, "x", keyform.x) &&
                        same_number_member(candidate, "y", keyform.y);
                });
            Value::Object keyform_object = source != nullptr
                ? source_object(*source)
                : Value::Object{};
            keyform_object["x"] = number_value(keyform.x);
            keyform_object["y"] = number_value(keyform.y);
            keyform_object["control_points"] = point_array(keyform.control_points);
            keyforms.push_back(object_value(std::move(keyform_object)));
        }
    } else {
        object.erase("grid_cols");
        object.erase("grid_rows");
        object.erase("control_points");
        object["pivot"] = point_array({definition.pivot});
        object["influence"] = number_value(definition.influence);
        keyforms.reserve(definition.rotation_keyforms.size());
        for (const runtime::RotationDeformerKeyform& keyform : definition.rotation_keyforms) {
            const Value* source = source_array_match(
                definition.preserved_source, "keyforms", [&](const Value& candidate) {
                    return same_number_member(candidate, "value", keyform.value);
                });
            Value::Object keyform_object = source != nullptr
                ? source_object(*source)
                : Value::Object{};
            keyform_object["value"] = number_value(keyform.value);
            keyform_object["angle"] = number_value(keyform.angle);
            keyforms.push_back(object_value(std::move(keyform_object)));
        }
    }
    object["keyforms"] = array_value(std::move(keyforms));
    return object_value(std::move(object));
}

bool parse_art_path_authoring_value(
    const Value& value,
    ArtPathAuthoringDefinition* definition_out,
    std::string* error_out) {
    if (definition_out == nullptr || !require_object(value, "art path", error_out)) return false;
    ArtPathAuthoringDefinition definition;
    definition.preserved_source = value;
    std::string cap;
    std::string join;
    if (!read_string(value, "id", &definition.id, error_out) ||
        !read_string(value, "name", &definition.name, error_out) ||
        !read_optional_string(
            value, "parent_deformer", &definition.parent_deformer, error_out) ||
        !read_points(value, "points", &definition.points, error_out) ||
        !read_number(value, "width", &definition.width, error_out) ||
        !read_color(value, "color", &definition.color, error_out) ||
        !read_string(value, "cap", &cap, error_out) ||
        !read_string(value, "join", &join, error_out)) {
        return false;
    }
    if (cap == "butt") definition.cap = runtime::ArtPathCap::Butt;
    else if (cap == "square") definition.cap = runtime::ArtPathCap::Square;
    else if (cap == "round") definition.cap = runtime::ArtPathCap::Round;
    else return fail(error_out, "cap must be butt, square, or round");
    if (join == "miter") definition.join = runtime::ArtPathJoin::Miter;
    else if (join == "bevel") definition.join = runtime::ArtPathJoin::Bevel;
    else if (join == "round") definition.join = runtime::ArtPathJoin::Round;
    else return fail(error_out, "join must be miter, bevel, or round");

    if (const Value* parameter_keyforms = member(value, "parameter_keyforms")) {
        if (!parameter_keyforms->is_object()) {
            return fail(error_out, "parameter_keyforms must be an object");
        }
        runtime::ArtPathParameterKeyforms parsed;
        if (!read_string(
                *parameter_keyforms, "parameter", &parsed.parameter, error_out)) {
            return false;
        }
        const Value* keyforms = member(*parameter_keyforms, "keyforms");
        if (keyforms == nullptr || !keyforms->is_array()) {
            return fail(error_out, "parameter_keyforms.keyforms must be an array");
        }
        for (const Value& keyform_value : keyforms->as_array()) {
            if (!keyform_value.is_object()) {
                return fail(error_out, "art path keyforms must be objects");
            }
            runtime::ArtPathKeyform keyform;
            if (!read_number(keyform_value, "value", &keyform.value, error_out) ||
                !read_points(keyform_value, "points", &keyform.points, error_out) ||
                !read_number(keyform_value, "width", &keyform.width, error_out) ||
                !read_color(keyform_value, "color", &keyform.color, error_out)) {
                return false;
            }
            parsed.keyforms.push_back(std::move(keyform));
        }
        definition.parameter_keyforms = std::move(parsed);
    }
    *definition_out = std::move(definition);
    return true;
}

Value build_art_path_authoring_value(const ArtPathAuthoringDefinition& definition) {
    Value::Object object = source_object(definition.preserved_source);
    object["id"] = string_value(definition.id);
    object["name"] = string_value(definition.name);
    if (definition.parent_deformer.has_value()) {
        object["parent_deformer"] = string_value(*definition.parent_deformer);
    } else {
        object.erase("parent_deformer");
    }
    object["points"] = point_array(definition.points);
    object["width"] = number_value(definition.width);
    object["color"] = color_value(
        definition.color, member(definition.preserved_source, "color"));
    const char* cap = definition.cap == runtime::ArtPathCap::Round
        ? "round"
        : definition.cap == runtime::ArtPathCap::Square ? "square" : "butt";
    const char* join = definition.join == runtime::ArtPathJoin::Round
        ? "round"
        : definition.join == runtime::ArtPathJoin::Bevel ? "bevel" : "miter";
    object["cap"] = string_value(cap);
    object["join"] = string_value(join);

    if (!definition.parameter_keyforms.has_value()) {
        object.erase("parameter_keyforms");
    } else {
        const Value* source = member(definition.preserved_source, "parameter_keyforms");
        Value::Object parameter_object = source != nullptr
            ? source_object(*source)
            : Value::Object{};
        parameter_object["parameter"] =
            string_value(definition.parameter_keyforms->parameter);
        Value::Array keyforms;
        keyforms.reserve(definition.parameter_keyforms->keyforms.size());
        for (const runtime::ArtPathKeyform& keyform :
             definition.parameter_keyforms->keyforms) {
            const Value* keyform_source = source != nullptr
                ? source_array_match(*source, "keyforms", [&](const Value& candidate) {
                      return same_number_member(candidate, "value", keyform.value);
                  })
                : nullptr;
            Value::Object keyform_object = keyform_source != nullptr
                ? source_object(*keyform_source)
                : Value::Object{};
            keyform_object["value"] = number_value(keyform.value);
            keyform_object["points"] = point_array(keyform.points);
            keyform_object["width"] = number_value(keyform.width);
            keyform_object["color"] = color_value(
                keyform.color,
                keyform_source != nullptr ? member(*keyform_source, "color") : nullptr);
            keyforms.push_back(object_value(std::move(keyform_object)));
        }
        parameter_object["keyforms"] = array_value(std::move(keyforms));
        object["parameter_keyforms"] = object_value(std::move(parameter_object));
    }
    return object_value(std::move(object));
}

bool parse_expression_authoring_value(
    const Value& value,
    ExpressionAuthoringDefinition* definition_out,
    std::string* error_out) {
    if (definition_out == nullptr || !require_object(value, "expression", error_out)) return false;
    ExpressionAuthoringDefinition definition;
    definition.preserved_source = value;
    std::string blend;
    std::string reset_policy;
    if (!read_string(value, "id", &definition.id, error_out) ||
        !read_string(value, "name", &definition.name, error_out) ||
        !read_number(value, "duration", &definition.duration, error_out) ||
        !read_string(value, "blend", &blend, error_out) ||
        !read_int(value, "priority", &definition.priority, error_out) ||
        !read_string(value, "reset_policy", &reset_policy, error_out)) {
        return false;
    }
    if (blend == "additive") definition.blend = runtime::ExpressionBlend::Additive;
    else if (blend == "override") definition.blend = runtime::ExpressionBlend::Override;
    else return fail(error_out, "blend must be additive or override");
    if (reset_policy == "restore") {
        definition.reset_policy = runtime::ExpressionResetPolicy::Restore;
    } else if (reset_policy == "hold") {
        definition.reset_policy = runtime::ExpressionResetPolicy::Hold;
    } else {
        return fail(error_out, "reset_policy must be restore or hold");
    }
    const Value* targets = member(value, "targets");
    if (targets == nullptr || !targets->is_array()) {
        return fail(error_out, "targets must be an array");
    }
    for (const Value& target_value : targets->as_array()) {
        if (!target_value.is_object()) return fail(error_out, "expression targets must be objects");
        runtime::ExpressionTargetDefinition target;
        if (!read_string(target_value, "parameter", &target.parameter, error_out) ||
            !read_number(target_value, "value", &target.value, error_out)) {
            return false;
        }
        definition.targets.push_back(std::move(target));
    }
    *definition_out = std::move(definition);
    return true;
}

Value build_expression_authoring_value(const ExpressionAuthoringDefinition& definition) {
    Value::Object object = source_object(definition.preserved_source);
    object["id"] = string_value(definition.id);
    object["name"] = string_value(definition.name);
    Value::Array targets;
    targets.reserve(definition.targets.size());
    for (const runtime::ExpressionTargetDefinition& target : definition.targets) {
        const Value* source = source_array_match(
            definition.preserved_source, "targets", [&](const Value& candidate) {
                const Value* parameter = member(candidate, "parameter");
                return parameter != nullptr && parameter->is_string() &&
                    parameter->as_string() == target.parameter;
            });
        Value::Object target_object = source != nullptr
            ? source_object(*source)
            : Value::Object{};
        target_object["parameter"] = string_value(target.parameter);
        target_object["value"] = number_value(target.value);
        targets.push_back(object_value(std::move(target_object)));
    }
    object["targets"] = array_value(std::move(targets));
    object["duration"] = number_value(definition.duration);
    object["blend"] = string_value(
        definition.blend == runtime::ExpressionBlend::Override ? "override" : "additive");
    object["priority"] = number_value(static_cast<double>(definition.priority));
    object["reset_policy"] = string_value(
        definition.reset_policy == runtime::ExpressionResetPolicy::Hold ? "hold" : "restore");
    return object_value(std::move(object));
}

bool parse_lip_sync_authoring_value(
    const Value& value,
    LipSyncAuthoringDefinition* definition_out,
    std::string* error_out) {
    if (definition_out == nullptr || !require_object(value, "lip_sync", error_out)) return false;
    LipSyncAuthoringDefinition definition;
    definition.preserved_source = value;
    const Value* mappings = member(value, "mappings");
    if (mappings == nullptr) {
        *definition_out = std::move(definition);
        return true;
    }
    if (!mappings->is_array()) return fail(error_out, "mappings must be an array");
    for (const Value& mapping_value : mappings->as_array()) {
        if (!mapping_value.is_object()) return fail(error_out, "lip-sync mappings must be objects");
        LipSyncMappingAuthoringDefinition mapping;
        mapping.preserved_source = mapping_value;
        std::string source;
        if (!read_string(mapping_value, "source", &source, error_out) ||
            !read_string(mapping_value, "parameter", &mapping.parameter, error_out) ||
            !read_optional_number(mapping_value, "scale", 1.0, &mapping.scale, error_out) ||
            !read_optional_number(mapping_value, "bias", 0.0, &mapping.bias, error_out) ||
            !read_optional_number(
                mapping_value, "smoothing", 0.0, &mapping.smoothing, error_out) ||
            !read_optional_number(mapping_value, "attack", 0.0, &mapping.attack, error_out) ||
            !read_optional_number(mapping_value, "release", 0.0, &mapping.release, error_out)) {
            return false;
        }
        if (source == "amplitude") mapping.source = runtime::LipSyncSource::Amplitude;
        else if (source == "phoneme") mapping.source = runtime::LipSyncSource::Phoneme;
        else return fail(error_out, "source must be amplitude or phoneme");
        if (const Value* phoneme_map = member(mapping_value, "phoneme_map")) {
            if (!phoneme_map->is_object()) return fail(error_out, "phoneme_map must be an object");
            for (const auto& [phoneme, phoneme_value] : phoneme_map->as_object()) {
                if (!phoneme_value.is_number() || !std::isfinite(phoneme_value.as_number())) {
                    return fail(error_out, "phoneme_map values must be finite numbers");
                }
                mapping.phoneme_map.push_back({phoneme, phoneme_value.as_number()});
            }
        }
        definition.mappings.push_back(std::move(mapping));
    }
    *definition_out = std::move(definition);
    return true;
}

Value build_lip_sync_authoring_value(const LipSyncAuthoringDefinition& definition) {
    Value::Object object = source_object(definition.preserved_source);
    Value::Array mappings;
    mappings.reserve(definition.mappings.size());
    for (const LipSyncMappingAuthoringDefinition& mapping : definition.mappings) {
        Value::Object mapping_object = source_object(mapping.preserved_source);
        mapping_object["source"] = string_value(
            mapping.source == runtime::LipSyncSource::Phoneme ? "phoneme" : "amplitude");
        mapping_object["parameter"] = string_value(mapping.parameter);
        mapping_object["scale"] = number_value(mapping.scale);
        mapping_object["bias"] = number_value(mapping.bias);
        mapping_object["smoothing"] = number_value(mapping.smoothing);
        mapping_object["attack"] = number_value(mapping.attack);
        mapping_object["release"] = number_value(mapping.release);
        if (mapping.phoneme_map.empty()) {
            mapping_object.erase("phoneme_map");
        } else {
            Value::Object phonemes;
            for (const runtime::PhonemeValueDefinition& phoneme : mapping.phoneme_map) {
                phonemes[phoneme.phoneme] = number_value(phoneme.value);
            }
            mapping_object["phoneme_map"] = object_value(std::move(phonemes));
        }
        mappings.push_back(object_value(std::move(mapping_object)));
    }
    object["mappings"] = array_value(std::move(mappings));
    return object_value(std::move(object));
}

} // namespace marrow::editor
