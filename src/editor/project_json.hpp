#pragma once

// Private project JSON vocabulary and encodings shared by project/runtime writers.
// No filesystem access, UI state, or public ABI is introduced here.
#include "marrow/editor/project.hpp"
#include <array>

namespace marrow::editor::project_detail {
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

inline constexpr std::string_view kInheritModeMessage =
    "inherit mode must be one of normal, onlyTranslation, "
    "noRotationOrReflection, noScale, or noScaleOrReflection";

inline constexpr std::array<ConstraintKind, 4> kConstraintFamilies{
    ConstraintKind::Ik,
    ConstraintKind::Path,
    ConstraintKind::Transform,
    ConstraintKind::Physics,
};


LoadError validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message);

const Value* find_optional_member(const Value& object, std::string_view key);

std::string_view transform_channel_json_key(TransformTimelineChannel channel);

std::optional<TransformTimelineChannel> transform_channel_from_key(std::string_view key);

std::optional<runtime::BoneInherit> inherit_mode_from_key_impl(std::string_view key);

std::string_view inherit_mode_json_key_impl(runtime::BoneInherit inherit);

bool finite_animation_scalar(double value);

std::string_view onion_skin_mode_json_key(OnionSkinMode mode);

std::optional<OnionSkinMode> onion_skin_mode_from_key(std::string_view key);

std::string_view path_spacing_mode_json_key(runtime::PathConstraintSpacingMode mode);

std::optional<runtime::PathConstraintSpacingMode> path_spacing_mode_from_key(
    std::string_view key);

bool is_vector_channel(TransformTimelineChannel channel);

Value make_null_value();

Value make_boolean_value(bool value);

Value make_number_value(double value);

Value make_string_value(std::string value);

Value make_array_value(Value::Array values = {});

Value make_object_value(Value::Object values = {});

Value* ensure_object_member(Value* object, std::string_view key);

Value* ensure_array_member(Value* object, std::string_view key);

Value build_interpolation_value(const runtime::Interpolation& interpolation);

std::string_view constraint_family_json_key(ConstraintKind family);

std::optional<ConstraintKind> constraint_family_from_json_key(std::string_view key);

Value build_deform_keyframes_value(const MeshDeformTimelineEdit& edit);

Value build_mesh_weight_vertices_value(const MeshWeightAttachmentEdit& edit);

Value build_draw_order_keyframes_value(const DrawOrderTimelineEdit& edit);

Value build_event_keyframes_value(const EventTimelineEdit& edit);

Value build_slot_attachment_keyframes_value(const SlotAttachmentTimelineEdit& edit);

Value build_inherit_keyframes_value(const BoneInheritTimelineEdit& edit);

Value build_ik_constraint_edits_value(const std::vector<IkConstraintEdit>& edits);

Value build_path_constraint_edits_value(const std::vector<PathConstraintEdit>& edits);

Value build_transform_constraint_edits_value(const std::vector<TransformConstraintEdit>& edits);

Value build_physics_constraint_edits_value(const std::vector<PhysicsConstraintEdit>& edits);

Value build_parameter_definition_value(
    const ParameterModel& model,
    const ParameterAuthoringDefinition& parameter);

Value build_parameter_group_value(
    const ParameterModel& model,
    const ParameterGroupAuthoringDefinition& group);

} // namespace marrow::editor::project_detail
