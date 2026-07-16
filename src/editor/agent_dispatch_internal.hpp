#pragma once

#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/editor/session.hpp"

namespace marrow::editor::agent_detail {

namespace json = marrow::runtime::json;

struct OperationSpec;
using OperationHandler = AgentDispatchResult (*)(
    AgentCommandContext&,
    const json::Value&,
    const OperationSpec&);

/** Internal registry row. Public protocol metadata and executable behavior live together. */
struct OperationSpec {
    std::string_view name;
    std::string_view category;
    bool mutating{false};
    bool requires_review{false};
    bool dry_run_supported{false};
    bool requires_project{true};
    OperationHandler handler{nullptr};
};

json::Value object_value(json::Value::Object object);
json::Value array_value(json::Value::Array array);
json::Value string_value(std::string value);
json::Value number_value(std::size_t value);
json::Value number_value(double value);
json::Value bool_value(bool value);

AgentDispatchResult make_error(
    std::string message,
    std::string_view op = {},
    const OperationSpec* spec = nullptr,
    std::string error_code = "invalid_request");
AgentDispatchResult make_error_with_delta(
    std::string message,
    std::string_view op,
    const OperationSpec* spec,
    json::Value delta,
    std::string error_code = "invalid_request");
AgentDispatchResult make_success(
    std::string message,
    std::string_view op,
    const OperationSpec* spec,
    json::Value delta = json::Value());

const json::Value* command_args(const json::Value& cmd);
bool bool_arg(
    const json::Value* args,
    std::string_view name,
    bool default_value = false);
std::optional<std::string_view> string_arg(
    const json::Value& args,
    std::string_view name);
const json::Value* find_member_any(
    const json::Value& args,
    std::string_view primary,
    std::string_view fallback = {});
std::optional<std::string_view> string_arg_any(
    const json::Value& args,
    std::initializer_list<std::string_view> names);
std::optional<std::filesystem::path> path_arg_any(
    const json::Value& args,
    std::initializer_list<std::string_view> names);
std::optional<double> number_arg(const json::Value& args, std::string_view name);
bool apply_number_if_present(
    const json::Value& args,
    std::string_view primary,
    std::string_view fallback,
    double* target,
    std::string* error_out);
std::optional<int> integer_arg(const json::Value& args, std::string_view name);
std::optional<marrow::runtime::Interpolation> interpolation_arg(
    const json::Value& args,
    std::string_view name,
    std::string* error_out);
bool parse_number_array(
    const json::Value& args,
    std::string_view name,
    std::size_t max_count,
    std::vector<double>* values_out,
    std::string* error_out);
std::optional<marrow::runtime::SlotColor> color_arg(
    const json::Value& args,
    std::string_view name,
    std::string* error_out);

bool ensure_project_loaded(const EditorSession& session);
std::vector<std::string> names_from_indices(
    const std::vector<marrow::runtime::BoneData>& bones,
    const std::vector<std::size_t>& indices);
json::Value string_array_value(const std::vector<std::string>& values);
std::string attachment_kind_name(marrow::runtime::AttachmentKind kind);

std::filesystem::path absolute_normalized(const std::filesystem::path& path);
bool agent_path_allowed(
    const EditorSession& session,
    const std::filesystem::path& target_path);
AgentDispatchResult enqueue_review(
    AgentCommandContext& context,
    std::string_view op,
    const OperationSpec* spec,
    AgentReviewKind kind,
    std::string label,
    std::filesystem::path target_path,
    bool binary_output,
    std::vector<std::filesystem::path> target_paths = {},
    std::string args_summary = {});

json::Value operation_specs_value();
json::Value slots_value(const marrow::runtime::SkeletonData& skeleton);
json::Value skins_value(const marrow::runtime::SkeletonData& skeleton);
json::Value attachments_value(
    const marrow::runtime::SkeletonData& skeleton,
    const json::Value* args);
json::Value constraints_value(const marrow::runtime::SkeletonData& skeleton);

std::optional<DrawOrderTimelineEdit> draw_order_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view animation_name);
bool parse_complete_slot_order(
    const marrow::runtime::SkeletonData& skeleton,
    const json::Value& args,
    std::vector<std::string>* slot_order_out,
    std::string* error_out);
const marrow::runtime::AttachmentData* find_mesh_attachment(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    std::optional<std::size_t>* slot_index_out = nullptr);
MeshWeightAttachmentEdit mesh_weight_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    const marrow::runtime::AttachmentData& attachment);
void normalize_weight_vertex(MeshWeightVertexEdit* vertex);
MeshWeightAttachmentEdit* ensure_mesh_weight_edit(
    ProjectData& project,
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    const marrow::runtime::AttachmentData& attachment);

const marrow::runtime::PathConstraintData* find_runtime_path_constraint(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view name);
const marrow::runtime::TransformConstraintData* find_runtime_transform_constraint(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view name);
const marrow::runtime::PhysicsConstraintData* find_runtime_physics_constraint(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view name);
PathConstraintEdit path_constraint_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::PathConstraintData& constraint);
TransformConstraintEdit transform_constraint_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::TransformConstraintData& constraint);
PhysicsConstraintEdit physics_constraint_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::PhysicsConstraintData& constraint);
bool merge_path_constraint_args(
    const json::Value& args,
    const marrow::runtime::SkeletonData& skeleton,
    PathConstraintEdit* edit,
    std::string* error_out);
bool merge_transform_constraint_args(
    const json::Value& args,
    const marrow::runtime::SkeletonData& skeleton,
    TransformConstraintEdit* edit,
    std::string* error_out);
bool merge_physics_constraint_args(
    const json::Value& args,
    const marrow::runtime::SkeletonData& skeleton,
    PhysicsConstraintEdit* edit,
    std::string* error_out);
json::Value path_constraint_preview_value(const PathConstraintEdit& edit, bool dry_run);
json::Value transform_constraint_preview_value(
    const TransformConstraintEdit& edit,
    bool dry_run);
json::Value physics_constraint_preview_value(
    const PhysicsConstraintEdit& edit,
    bool dry_run);
json::Value timeline_description_value(
    const marrow::runtime::SkeletonData& skeleton,
    const ProjectData& project,
    std::string_view animation_name);

AgentDispatchResult handle_inspection_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation);
AgentDispatchResult handle_editing_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation);
AgentDispatchResult handle_timeline_editing_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation);
AgentDispatchResult handle_constraint_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation);
AgentDispatchResult handle_parameter_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation);
AgentDispatchResult handle_management_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation);

} // namespace marrow::editor::agent_detail
