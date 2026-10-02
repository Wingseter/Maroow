#include "project_json.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace marrow::editor::project_detail {

LoadError validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message) {
    return marrow::runtime::json::make_validation_error(
        document, location, std::move(json_path), std::move(message));
}

const Value* find_optional_member(const Value& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }

    return marrow::runtime::json::find_member(object, key);
}

std::string_view transform_channel_json_key(TransformTimelineChannel channel) {
    switch (channel) {
    case TransformTimelineChannel::Rotate:
        return "rotate";
    case TransformTimelineChannel::Translate:
        return "translate";
    case TransformTimelineChannel::Scale:
        return "scale";
    case TransformTimelineChannel::Shear:
        return "shear";
    }

    return "rotate";
}

std::optional<TransformTimelineChannel> transform_channel_from_key(std::string_view key) {
    if (key == "rotate") {
        return TransformTimelineChannel::Rotate;
    }
    if (key == "translate") {
        return TransformTimelineChannel::Translate;
    }
    if (key == "scale") {
        return TransformTimelineChannel::Scale;
    }
    if (key == "shear") {
        return TransformTimelineChannel::Shear;
    }

    return std::nullopt;
}

// MAR-184: the five stepped inherit tokens, both directions, from ONE table.
// The vocabulary is exactly the runtime's (`skeleton_parse.cpp:494-511`), whose
// `parse_bone_inherit` lives in an anonymous namespace and has no inverse -- so
// it cannot be reached from here, and a one-way copy would be free to drift.
// The namespace-scope wrappers below this file's anonymous namespace forward to
// these rows, so the `.marrow` parser, both serializers, and
// `merge_inherit_timeline` in `authoring.cpp` all read the same vocabulary.
struct InheritModeRow {
    std::string_view key;
    runtime::BoneInherit inherit;
};

constexpr std::array<InheritModeRow, 5> kInheritModeTable{{
    {"normal", runtime::BoneInherit::Normal},
    {"onlyTranslation", runtime::BoneInherit::OnlyTranslation},
    {"noRotationOrReflection", runtime::BoneInherit::NoRotationOrReflection},
    {"noScale", runtime::BoneInherit::NoScale},
    {"noScaleOrReflection", runtime::BoneInherit::NoScaleOrReflection},
}};

std::optional<runtime::BoneInherit> inherit_mode_from_key_impl(std::string_view key) {
    for (const InheritModeRow& row : kInheritModeTable) {
        if (row.key == key) {
            return row.inherit;
        }
    }
    return std::nullopt;
}

std::string_view inherit_mode_json_key_impl(runtime::BoneInherit inherit) {
    for (const InheritModeRow& row : kInheritModeTable) {
        if (row.inherit == inherit) {
            return row.key;
        }
    }
    return kInheritModeTable[0].key;
}

/**
 * @brief Reports whether `value` is finite and fits a runtime float32 key.
 *
 * A file-local copy of the helper `authoring.cpp:322` and `curve_auto.cpp:40`
 * each already carry: both live in their own anonymous namespaces and neither
 * is declared in a header, so a third file-local copy follows the repo's
 * existing convention rather than inventing one. The float32 bound is what
 * makes the check reachable from a `.marrow` at all -- JSON has no NaN literal
 * and `json.cpp:302-307` refuses an out-of-range exponent at the tokenizer, so
 * an over-large finite magnitude such as `1e39` is the only value a file can
 * present. NaN and infinity reach the same rule only through
 * `merge_inherit_timeline`'s `double` parameter, from C++.
 */
bool finite_animation_scalar(double value) {
    return std::isfinite(value) &&
        std::abs(value) <=
            static_cast<double>(std::numeric_limits<runtime::AnimationScalar>::max());
}

std::string_view onion_skin_mode_json_key(OnionSkinMode mode) {
    switch (mode) {
    case OnionSkinMode::Frame:
        return "frame";
    case OnionSkinMode::Keyframe:
        return "keyframe";
    }

    return "frame";
}

std::optional<OnionSkinMode> onion_skin_mode_from_key(std::string_view key) {
    if (key == "frame") {
        return OnionSkinMode::Frame;
    }
    if (key == "keyframe") {
        return OnionSkinMode::Keyframe;
    }

    return std::nullopt;
}

std::string_view path_spacing_mode_json_key(runtime::PathConstraintSpacingMode mode) {
    switch (mode) {
    case runtime::PathConstraintSpacingMode::Length:
        return "length";
    case runtime::PathConstraintSpacingMode::Percent:
        return "percent";
    }

    return "length";
}

std::optional<runtime::PathConstraintSpacingMode> path_spacing_mode_from_key(
    std::string_view key) {
    if (key == "length") {
        return runtime::PathConstraintSpacingMode::Length;
    }
    if (key == "percent") {
        return runtime::PathConstraintSpacingMode::Percent;
    }

    return std::nullopt;
}

bool is_vector_channel(TransformTimelineChannel channel) {
    return channel != TransformTimelineChannel::Rotate;
}

Value make_null_value() {
    return Value(nullptr, SourceLocation{});
}

Value make_boolean_value(bool value) {
    return Value(value, SourceLocation{});
}

Value make_number_value(double value) {
    return Value(value, SourceLocation{});
}

Value make_string_value(std::string value) {
    return Value(std::move(value), SourceLocation{});
}

Value make_array_value(Value::Array values) {
    return Value(std::move(values), SourceLocation{});
}

Value make_object_value(Value::Object values) {
    return Value(std::move(values), SourceLocation{});
}

Value* ensure_object_member(Value* object, std::string_view key) {
    if (object == nullptr) {
        return nullptr;
    }
    if (!object->is_object()) {
        *object = make_object_value();
    }

    auto& members = object->as_object();
    auto [iterator, inserted] =
        members.emplace(std::string(key), make_object_value());
    if (!inserted && !iterator->second.is_object()) {
        iterator->second = make_object_value();
    }

    return &iterator->second;
}

Value* ensure_array_member(Value* object, std::string_view key) {
    if (object == nullptr) {
        return nullptr;
    }
    if (!object->is_object()) {
        *object = make_object_value();
    }

    auto& members = object->as_object();
    auto [iterator, inserted] =
        members.emplace(std::string(key), make_array_value());
    if (!inserted && !iterator->second.is_array()) {
        iterator->second = make_array_value();
    }

    return &iterator->second;
}

Value build_interpolation_value(const runtime::Interpolation& interpolation) {
    switch (interpolation.kind()) {
    case runtime::InterpolationKind::Linear:
        return make_string_value("linear");
    case runtime::InterpolationKind::Stepped:
        return make_string_value("stepped");
    case runtime::InterpolationKind::CubicBezier: {
        const auto& bezier = interpolation.cubic_bezier();
        Value::Array control_points;
        control_points.reserve(4);
        control_points.push_back(make_number_value(bezier.cx1));
        control_points.push_back(make_number_value(bezier.cy1));
        control_points.push_back(make_number_value(bezier.cx2));
        control_points.push_back(make_number_value(bezier.cy2));
        return make_array_value(std::move(control_points));
    }
    }

    return make_string_value("linear");
}

// The one mapping between a constraint family and the root array that holds
// it. The parser, the serializer, the lifecycle materializer, both validators,
// and the two project primitives all read it, so "which key is this family"
// has exactly one answer.
std::string_view constraint_family_json_key(ConstraintKind family) {
    switch (family) {
    case ConstraintKind::Ik:
        return "ik";
    case ConstraintKind::Path:
        return "path";
    case ConstraintKind::Transform:
        return "transform";
    case ConstraintKind::Physics:
        return "physics";
    }
    return "ik";
}

std::optional<ConstraintKind> constraint_family_from_json_key(std::string_view key) {
    if (key == "ik") return ConstraintKind::Ik;
    if (key == "path") return ConstraintKind::Path;
    if (key == "transform") return ConstraintKind::Transform;
    if (key == "physics") return ConstraintKind::Physics;
    return std::nullopt;
}

Value build_deform_keyframes_value(const MeshDeformTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const DeformKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));

        Value::Array vertex_offsets;
        vertex_offsets.reserve(keyframe.vertex_offsets.size());
        for (const double value : keyframe.vertex_offsets) {
            vertex_offsets.push_back(make_number_value(value));
        }
        keyframe_object.emplace("vertices", make_array_value(std::move(vertex_offsets)));
        keyframe_object.emplace("curve", build_interpolation_value(keyframe.interpolation));
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }

    return make_array_value(std::move(keyframes));
}

Value build_mesh_weight_vertices_value(const MeshWeightAttachmentEdit& edit) {
    Value::Array vertices;
    vertices.reserve(edit.vertices.size());
    for (const MeshWeightVertexEdit& vertex : edit.vertices) {
        Value::Array influences;
        influences.reserve(vertex.influences.size());
        for (const MeshWeightInfluenceEdit& influence : vertex.influences) {
            Value::Object influence_object;
            influence_object.emplace("bone", make_string_value(influence.bone_name));
            influence_object.emplace("x", make_number_value(influence.x));
            influence_object.emplace("y", make_number_value(influence.y));
            influence_object.emplace("weight", make_number_value(influence.weight));
            influences.push_back(make_object_value(std::move(influence_object)));
        }
        vertices.push_back(make_array_value(std::move(influences)));
    }

    return make_array_value(std::move(vertices));
}

Value build_draw_order_keyframes_value(const DrawOrderTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const DrawOrderKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));

        Value::Array slots;
        slots.reserve(keyframe.slot_names.size());
        for (const std::string& slot_name : keyframe.slot_names) {
            slots.push_back(make_string_value(slot_name));
        }
        keyframe_object.emplace("slots", make_array_value(std::move(slots)));
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }

    return make_array_value(std::move(keyframes));
}

Value build_event_keyframes_value(const EventTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const EventKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        keyframe_object.emplace("name", make_string_value(keyframe.event_name));
        if (keyframe.int_value.has_value()) {
            keyframe_object.emplace("int", make_number_value(*keyframe.int_value));
        }
        if (keyframe.float_value.has_value()) {
            keyframe_object.emplace("float", make_number_value(*keyframe.float_value));
        }
        if (keyframe.string_value.has_value()) {
            keyframe_object.emplace("string", make_string_value(*keyframe.string_value));
        }
        if (keyframe.audio_path.has_value()) {
            keyframe_object.emplace("audio", make_string_value(*keyframe.audio_path));
        }
        if (keyframe.volume.has_value()) {
            keyframe_object.emplace("volume", make_number_value(*keyframe.volume));
        }
        if (keyframe.balance.has_value()) {
            keyframe_object.emplace("balance", make_number_value(*keyframe.balance));
        }
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }

    return make_array_value(std::move(keyframes));
}

Value build_slot_attachment_keyframes_value(const SlotAttachmentTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const SlotAttachmentKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        if (keyframe.attachment_name.has_value()) {
            keyframe_object.emplace("attachment", make_string_value(*keyframe.attachment_name));
        } else {
            keyframe_object.emplace("attachment", make_null_value());
        }
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }
    return make_array_value(std::move(keyframes));
}

/**
 * @brief Encodes one stepped inherit timeline.
 *
 * ONE builder serves both `.marrow` and `.mskl`. Transform and slot color each
 * need a `build_runtime_*` variant to strip `curve_mode`/`curve_driver`;
 * inherit carries no project-only member, so the two encodings are the same.
 */
Value build_inherit_keyframes_value(const BoneInheritTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const InheritKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        keyframe_object.emplace(
            "inherit",
            make_string_value(std::string(inherit_mode_json_key_impl(keyframe.inherit))));
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }
    return make_array_value(std::move(keyframes));
}

Value build_ik_constraint_edits_value(const std::vector<IkConstraintEdit>& edits) {
    Value::Array constraints;
    constraints.reserve(edits.size());
    for (const IkConstraintEdit& edit : edits) {
        Value::Object constraint_object;
        constraint_object.emplace("name", make_string_value(edit.name));
        Value::Array bones;
        bones.reserve(edit.bone_names.size());
        for (const std::string& bone_name : edit.bone_names) {
            bones.push_back(make_string_value(bone_name));
        }
        constraint_object.emplace("bones", make_array_value(std::move(bones)));
        constraint_object.emplace("target", make_string_value(edit.target_bone_name));
        constraint_object.emplace("mix", make_number_value(edit.mix));
        constraint_object.emplace("bendPositive", make_boolean_value(edit.bend_positive));
        constraint_object.emplace("softness", make_number_value(edit.softness));
        constraint_object.emplace("compress", make_boolean_value(edit.compress));
        constraint_object.emplace("stretch", make_boolean_value(edit.stretch));
        constraints.push_back(make_object_value(std::move(constraint_object)));
    }

    return make_array_value(std::move(constraints));
}

Value build_path_constraint_edits_value(const std::vector<PathConstraintEdit>& edits) {
    Value::Array constraints;
    constraints.reserve(edits.size());
    for (const PathConstraintEdit& edit : edits) {
        Value::Object constraint_object;
        constraint_object.emplace("name", make_string_value(edit.name));
        constraint_object.emplace("slot", make_string_value(edit.slot_name));
        Value::Array bones;
        bones.reserve(edit.bone_names.size());
        for (const std::string& bone_name : edit.bone_names) {
            bones.push_back(make_string_value(bone_name));
        }
        constraint_object.emplace("bones", make_array_value(std::move(bones)));
        constraint_object.emplace("position", make_number_value(edit.position));
        constraint_object.emplace("spacing", make_number_value(edit.spacing));
        constraint_object.emplace(
            "spacingMode",
            make_string_value(std::string(path_spacing_mode_json_key(edit.spacing_mode))));
        constraint_object.emplace("rotateMix", make_number_value(edit.rotate_mix));
        constraint_object.emplace("translateMix", make_number_value(edit.translate_mix));
        constraints.push_back(make_object_value(std::move(constraint_object)));
    }

    return make_array_value(std::move(constraints));
}

Value build_transform_constraint_edits_value(const std::vector<TransformConstraintEdit>& edits) {
    Value::Array constraints;
    constraints.reserve(edits.size());
    for (const TransformConstraintEdit& edit : edits) {
        Value::Object constraint_object;
        constraint_object.emplace("name", make_string_value(edit.name));
        constraint_object.emplace("source", make_string_value(edit.source_bone_name));
        Value::Array bones;
        bones.reserve(edit.bone_names.size());
        for (const std::string& bone_name : edit.bone_names) {
            bones.push_back(make_string_value(bone_name));
        }
        constraint_object.emplace("bones", make_array_value(std::move(bones)));
        constraint_object.emplace("rotateMix", make_number_value(edit.rotate_mix));
        constraint_object.emplace("translateMix", make_number_value(edit.translate_mix));
        constraint_object.emplace("scaleMix", make_number_value(edit.scale_mix));
        constraint_object.emplace("shearMix", make_number_value(edit.shear_mix));
        Value::Object offset_object;
        offset_object.emplace("rotation", make_number_value(edit.offsets.rotation));
        offset_object.emplace("x", make_number_value(edit.offsets.x));
        offset_object.emplace("y", make_number_value(edit.offsets.y));
        offset_object.emplace("scaleX", make_number_value(edit.offsets.scale_x));
        offset_object.emplace("scaleY", make_number_value(edit.offsets.scale_y));
        offset_object.emplace("shearX", make_number_value(edit.offsets.shear_x));
        offset_object.emplace("shearY", make_number_value(edit.offsets.shear_y));
        constraint_object.emplace("offset", make_object_value(std::move(offset_object)));
        constraints.push_back(make_object_value(std::move(constraint_object)));
    }

    return make_array_value(std::move(constraints));
}

Value build_physics_constraint_edits_value(const std::vector<PhysicsConstraintEdit>& edits) {
    Value::Array constraints;
    constraints.reserve(edits.size());
    for (const PhysicsConstraintEdit& edit : edits) {
        Value::Object constraint_object;
        constraint_object.emplace("name", make_string_value(edit.name));
        Value::Array bones;
        bones.reserve(edit.bone_names.size());
        for (const std::string& bone_name : edit.bone_names) {
            bones.push_back(make_string_value(bone_name));
        }
        constraint_object.emplace("bones", make_array_value(std::move(bones)));
        constraint_object.emplace("step", make_number_value(edit.step));
        constraint_object.emplace("x", make_number_value(edit.x));
        constraint_object.emplace("y", make_number_value(edit.y));
        constraint_object.emplace("rotate", make_number_value(edit.rotate));
        constraint_object.emplace("scaleX", make_number_value(edit.scale_x));
        constraint_object.emplace("shearX", make_number_value(edit.shear_x));
        constraint_object.emplace("limit", make_number_value(edit.limit));
        constraint_object.emplace("inertia", make_number_value(edit.inertia));
        constraint_object.emplace("damping", make_number_value(edit.damping));
        constraint_object.emplace("strength", make_number_value(edit.strength));
        constraint_object.emplace("massInverse", make_number_value(edit.mass_inverse));
        Value::Object gravity_object;
        gravity_object.emplace("x", make_number_value(edit.gravity.x));
        gravity_object.emplace("y", make_number_value(edit.gravity.y));
        constraint_object.emplace("gravity", make_object_value(std::move(gravity_object)));
        Value::Object wind_object;
        wind_object.emplace("x", make_number_value(edit.wind.x));
        wind_object.emplace("y", make_number_value(edit.wind.y));
        constraint_object.emplace("wind", make_object_value(std::move(wind_object)));
        constraint_object.emplace("mix", make_number_value(edit.mix));
        constraints.push_back(make_object_value(std::move(constraint_object)));
    }

    return make_array_value(std::move(constraints));
}

static const Value* find_source_entry_by_id(
    const Value& source,
    std::string_view collection,
    std::string_view id) {
    const Value* values = find_optional_member(source, collection);
    if (values == nullptr || !values->is_array()) {
        return nullptr;
    }
    for (const Value& value : values->as_array()) {
        const Value* value_id = find_optional_member(value, "id");
        if (value_id != nullptr && value_id->is_string() && value_id->as_string() == id) {
            return &value;
        }
    }
    return nullptr;
}

Value build_parameter_definition_value(
    const ParameterModel& model,
    const ParameterAuthoringDefinition& parameter) {
    Value::Object object;
    if (const Value* source = find_source_entry_by_id(
            model.source, "parameters", parameter.id);
        source != nullptr && source->is_object()) {
        object = source->as_object();
    }
    object["id"] = make_string_value(parameter.id);
    object["name"] = make_string_value(parameter.name);
    object["min"] = make_number_value(parameter.min_value);
    object["max"] = make_number_value(parameter.max_value);
    object["default"] = make_number_value(parameter.default_value);
    object["type"] = make_string_value(
        parameter.type == ParameterAuthoringType::Continuous ? "continuous" : "discrete");
    object["clamp"] = make_boolean_value(parameter.clamp);
    if (parameter.ui_step.has_value()) {
        object["ui_step"] = make_number_value(*parameter.ui_step);
    } else {
        object.erase("ui_step");
    }
    if (parameter.units.has_value()) {
        object["units"] = make_string_value(*parameter.units);
    } else {
        object.erase("units");
    }
    return make_object_value(std::move(object));
}

Value build_parameter_group_value(
    const ParameterModel& model,
    const ParameterGroupAuthoringDefinition& group) {
    Value::Object object;
    if (const Value* source = find_source_entry_by_id(model.source, "groups", group.id);
        source != nullptr && source->is_object()) {
        object = source->as_object();
    }
    object["id"] = make_string_value(group.id);
    object["name"] = make_string_value(group.name);
    Value::Array parameter_ids;
    parameter_ids.reserve(group.parameter_ids.size());
    for (const std::string& parameter_id : group.parameter_ids) {
        parameter_ids.push_back(make_string_value(parameter_id));
    }
    object["parameters"] = make_array_value(std::move(parameter_ids));
    object["collapsed"] = make_boolean_value(group.collapsed);
    if (group.color_tag.has_value()) {
        object["color_tag"] = make_string_value(*group.color_tag);
    } else {
        object.erase("color_tag");
    }
    if (group.exclusive_mode.has_value()) {
        object["exclusive_mode"] = make_string_value(*group.exclusive_mode);
    } else {
        object.erase("exclusive_mode");
    }
    return make_object_value(std::move(object));
}

} // namespace marrow::editor::project_detail
