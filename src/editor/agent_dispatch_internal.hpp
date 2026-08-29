#pragma once

#include <cstddef>
#include <array>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/authoring.hpp"
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

enum class NoChangeResult {
    Error,
    Success,
};

struct CommitPolicy {
    std::string_view failure_prefix;
    std::string_view failure_error_code{"invalid_request"};
    NoChangeResult no_change_result{NoChangeResult::Error};
    std::string_view no_change_message{"No changes made."};
    std::string_view no_change_error_code{"no_change"};
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
std::optional<AgentDispatchResult> commit_or_error(
    EditorSession::EditTransaction& transaction,
    std::string_view op,
    const OperationSpec* spec,
    const CommitPolicy& policy);

const json::Value* command_args(const json::Value& cmd);
bool bool_arg(
    const json::Value* args,
    std::string_view name,
    bool default_value = false);
std::optional<std::string_view> string_arg(
    const json::Value& args,
    std::string_view name);
std::optional<std::string_view> string_arg_any(
    const json::Value& args,
    std::initializer_list<std::string_view> names);
std::optional<std::filesystem::path> path_arg_any(
    const json::Value& args,
    std::initializer_list<std::string_view> names);
std::optional<double> number_arg(const json::Value& args, std::string_view name);
std::optional<int> integer_arg(const json::Value& args, std::string_view name);
std::optional<marrow::runtime::Interpolation> interpolation_arg(
    const json::Value& args,
    std::string_view name,
    std::string* error_out);
/**
 * @brief Parses an easing request into a kind plus four raw control points.
 *
 * Unlike `interpolation_arg()`, a missing member is an error rather than a
 * silent linearization, because silently linearizing every selected key would
 * be a destructive default. The raw doubles are returned without constructing
 * a `runtime::Interpolation`, so a rejected request never enters the
 * process-wide cubic LUT cache and validation still sees the pre-narrowing
 * value.
 */
bool interpolation_request_arg(
    const json::Value& args,
    std::string_view name,
    marrow::runtime::InterpolationKind* kind_out,
    std::array<double, 4>* control_points_out,
    std::string* error_out);
/**
 * @brief Parses a curve-mode request into a mode plus an optional driver.
 *
 * A missing `mode` is an error rather than a silent default, because guessing
 * a mode for the caller's whole selection would be destructive. A driver
 * supplied with `manual` is rejected rather than ignored: it would record an
 * intent the mode says is inactive, and the shell never produces that pair.
 * This constructs nothing, so a rejected request touches no shared state.
 */
bool curve_mode_request_arg(
    const json::Value& args,
    marrow::editor::TimelineCurveMode* mode_out,
    std::optional<marrow::editor::TimelineScalarComponent>* driver_out,
    std::string* error_out);
/**
 * @brief Parses one lane selector array into project-domain lane selectors.
 *
 * Lane selectors carry no time, because loop synchronization is a property of
 * a whole timeline. Draw-order, event, and slot-attachment kinds are rejected
 * rather than ignored: those families are piecewise constant and need no
 * boundary key at all.
 */
bool timeline_lane_selectors_arg(
    const json::Value& args,
    std::vector<marrow::editor::TimelineLaneSelector>* lanes_out,
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
