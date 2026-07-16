#include "marrow/editor/agent_dispatch.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/editor/session.hpp"
#include "agent_dispatch_internal.hpp"

namespace marrow::editor {

namespace json = marrow::runtime::json;

namespace agent_detail {

constexpr double kKeyTimeEpsilon = 1e-6;
constexpr std::size_t kMaxAgentActivityEntries = 200;

constexpr OperationSpec kOperationSpecs[] = {
    {"operations.list", "inspection", false, false, false, false, &handle_management_operation},
    {"scene.describe", "inspection", false, false, false, true, &handle_inspection_operation},
    {"bones.list", "inspection", false, false, false, true, &handle_inspection_operation},
    {"animation.list", "inspection", false, false, false, true, &handle_inspection_operation},
    {"slots.list", "inspection", false, false, false, true, &handle_inspection_operation},
    {"skins.list", "inspection", false, false, false, true, &handle_inspection_operation},
    {"attachments.list", "inspection", false, false, false, true, &handle_inspection_operation},
    {"constraints.list", "inspection", false, false, false, true, &handle_inspection_operation},
    {"parameters.list", "inspection", false, false, false, true, &handle_parameter_operation},
    {"timeline.describe", "inspection", false, false, false, true, &handle_inspection_operation},
    {"mesh.describe", "inspection", false, false, false, true, &handle_inspection_operation},
    {"project.diagnostics", "inspection", false, false, false, true, &handle_inspection_operation},
    {"export.preview", "validation", false, false, false, true, &handle_inspection_operation},
    {"runtime.validate", "validation", false, false, false, true, &handle_inspection_operation},
    {"compare_runtime_export", "validation", false, false, false, true, &handle_inspection_operation},
    {"agent.permissions.describe", "management", false, false, false, false, &handle_management_operation},
    {"agent.pause", "management", false, false, false, false, &handle_management_operation},
    {"agent.resume", "management", false, false, false, false, &handle_management_operation},
    {"agent.terminate", "management", false, false, false, false, &handle_management_operation},
    {"undo", "edit", true, false, false, true, &handle_editing_operation},
    {"redo", "edit", true, false, false, true, &handle_editing_operation},
    {"parameter.set", "edit", true, false, true, true, &handle_parameter_operation},
    {"deformer.create", "edit", true, false, true, true, &handle_parameter_operation},
    {"keyform.capture", "edit", true, false, true, true, &handle_parameter_operation},
    {"expression.create", "edit", true, false, true, true, &handle_parameter_operation},
    {"lip_sync.map", "edit", true, false, true, true, &handle_parameter_operation},
    {"animation.create", "edit", true, false, true, true, &handle_editing_operation},
    {"animation.duplicate", "edit", true, false, true, true, &handle_editing_operation},
    {"animation.rename", "edit", true, false, true, true, &handle_editing_operation},
    {"animation.delete", "edit", true, false, true, true, &handle_editing_operation},
    {"timeline.retime_keyframes", "edit", true, false, true, true, &handle_editing_operation},
    {"set_transform", "edit", true, false, true, true, &handle_editing_operation},
    {"remove_transform_keyframe", "edit", true, false, false, true, &handle_editing_operation},
    {"set_event_keyframe", "edit", true, false, true, true, &handle_editing_operation},
    {"remove_event_keyframe", "edit", true, false, false, true, &handle_editing_operation},
    {"set_deform_keyframe", "edit", true, false, true, true, &handle_editing_operation},
    {"remove_deform_keyframe", "edit", true, false, false, true, &handle_editing_operation},
    {"set_vertex_weights", "edit", true, false, true, true, &handle_editing_operation},
    {"normalize_weights", "edit", true, false, true, true, &handle_editing_operation},
    {"edit_ik_constraint", "edit", true, false, true, true, &handle_constraint_operation},
    {"edit_path_constraint", "edit", true, false, true, true, &handle_constraint_operation},
    {"edit_transform_constraint", "edit", true, false, true, true, &handle_constraint_operation},
    {"edit_physics_constraint", "edit", true, false, true, true, &handle_constraint_operation},
    {"set_slot_color_keyframe", "edit", true, false, true, true, &handle_editing_operation},
    {"remove_slot_color_keyframe", "edit", true, false, false, true, &handle_editing_operation},
    {"set_attachment_keyframe", "edit", true, false, true, true, &handle_editing_operation},
    {"remove_attachment_keyframe", "edit", true, false, false, true, &handle_editing_operation},
    {"set_draw_order_keyframe", "edit", true, false, true, true, &handle_editing_operation},
    {"remove_draw_order_keyframe", "edit", true, false, false, true, &handle_editing_operation},
    {"save", "management", true, true, false, true, &handle_management_operation},
    {"export_runtime", "management", true, true, false, true, &handle_management_operation},
    {"import.spine_json", "management", true, true, true, true, &handle_management_operation},
    {"import.spine_atlas", "management", true, true, true, true, &handle_management_operation},
    {"import.psd_layers", "management", true, true, true, true, &handle_management_operation},
    {"atlas.pack", "management", true, true, true, true, &handle_management_operation},
};

const OperationSpec* find_operation(std::string_view op) {
    for (const OperationSpec& spec : kOperationSpecs) {
        if (spec.name == op) {
            return &spec;
        }
    }
    return nullptr;
}

bool op_allowed_while_paused(const OperationSpec& spec) {
    return !spec.mutating;
}

json::Value object_value(json::Value::Object object) {
    return json::Value(std::move(object), {});
}

json::Value array_value(json::Value::Array array) {
    return json::Value(std::move(array), {});
}

json::Value string_value(std::string value) {
    return json::Value(std::move(value), {});
}

json::Value number_value(std::size_t value) {
    return json::Value(static_cast<double>(value), {});
}

json::Value number_value(double value) {
    return json::Value(value, {});
}

json::Value bool_value(bool value) {
    return json::Value(value, {});
}

AgentDispatchResult make_result(
    bool ok,
    std::string message,
    std::string_view op,
    const OperationSpec* spec,
    json::Value scene_delta = json::Value(),
    std::string error_code = {}) {
    AgentDispatchResult result;
    result.ok = ok;
    result.message = std::move(message);
    result.scene_delta = std::move(scene_delta);
    result.op = std::string(op);
    if (spec != nullptr) {
        result.category = std::string(spec->category);
        result.mutating = spec->mutating;
        result.requires_review = spec->requires_review;
    }
    result.error_code = std::move(error_code);
    return result;
}

AgentDispatchResult make_error(
    std::string message,
    std::string_view op,
    const OperationSpec* spec,
    std::string error_code) {
    return make_result(false, std::move(message), op, spec, json::Value(), std::move(error_code));
}

AgentDispatchResult make_error_with_delta(
    std::string message,
    std::string_view op,
    const OperationSpec* spec,
    json::Value delta,
    std::string error_code) {
    return make_result(false, std::move(message), op, spec, std::move(delta), std::move(error_code));
}

AgentDispatchResult make_success(
    std::string message,
    std::string_view op,
    const OperationSpec* spec,
    json::Value delta) {
    return make_result(true, std::move(message), op, spec, std::move(delta));
}

const json::Value* command_args(const json::Value& cmd) {
    const json::Value* args = json::find_member(cmd, "args");
    return args != nullptr && args->is_object() ? args : nullptr;
}

bool bool_arg(const json::Value* args, std::string_view name, bool default_value) {
    if (args == nullptr) {
        return default_value;
    }
    const json::Value* value = json::find_member(*args, name);
    return value != nullptr && value->is_boolean() ? value->as_boolean() : default_value;
}

std::optional<std::string_view> string_arg(const json::Value& args, std::string_view name) {
    const json::Value* value = json::find_member(args, name);
    if (value == nullptr || !value->is_string()) {
        return std::nullopt;
    }
    return std::string_view(value->as_string());
}

const json::Value* find_member_any(
    const json::Value& args,
    std::string_view primary,
    std::string_view fallback) {
    if (const json::Value* value = json::find_member(args, primary)) {
        return value;
    }
    return fallback.empty() ? nullptr : json::find_member(args, fallback);
}

std::optional<std::string_view> string_arg_any(
    const json::Value& args,
    std::initializer_list<std::string_view> names) {
    for (std::string_view name : names) {
        if (const auto value = string_arg(args, name)) {
            return value;
        }
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> path_arg_any(
    const json::Value& args,
    std::initializer_list<std::string_view> names) {
    if (const auto value = string_arg_any(args, names)) {
        return std::filesystem::path(std::string(*value));
    }
    return std::nullopt;
}

std::optional<double> number_arg(const json::Value& args, std::string_view name) {
    const json::Value* value = json::find_member(args, name);
    if (value == nullptr || !value->is_number()) {
        return std::nullopt;
    }
    return value->as_number();
}

bool apply_number_if_present(
    const json::Value& args,
    std::string_view primary,
    std::string_view fallback,
    double* target,
    std::string* error_out) {
    const json::Value* value = find_member_any(args, primary, fallback);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_number()) {
        *error_out = std::string(primary) + " must be a number.";
        return false;
    }
    *target = value->as_number();
    return true;
}

std::optional<int> integer_arg(const json::Value& args, std::string_view name) {
    const std::optional<double> value = number_arg(args, name);
    if (!value.has_value() || std::abs(*value - std::round(*value)) > 1e-6) {
        return std::nullopt;
    }
    return static_cast<int>(std::round(*value));
}

std::optional<marrow::runtime::Interpolation> interpolation_arg(
    const json::Value& args,
    std::string_view name,
    std::string* error_out) {
    const json::Value* value = json::find_member(args, name);
    if (value == nullptr || value->is_null()) {
        return marrow::runtime::Interpolation::linear();
    }
    if (value->is_string()) {
        if (value->as_string() == "linear") {
            return marrow::runtime::Interpolation::linear();
        }
        if (value->as_string() == "stepped") {
            return marrow::runtime::Interpolation::stepped();
        }
        *error_out = "interpolation must be linear, stepped, or a 4-number bezier array.";
        return std::nullopt;
    }
    if (!value->is_array() || value->as_array().size() != 4U) {
        *error_out = "interpolation must be linear, stepped, or a 4-number bezier array.";
        return std::nullopt;
    }
    double coordinates[4] = {};
    for (std::size_t index = 0; index < 4U; ++index) {
        const json::Value& coordinate = value->as_array()[index];
        if (!coordinate.is_number()) {
            *error_out = "bezier interpolation values must be numbers.";
            return std::nullopt;
        }
        coordinates[index] = coordinate.as_number();
    }
    return marrow::runtime::Interpolation::cubic_bezier(
        coordinates[0], coordinates[1], coordinates[2], coordinates[3]);
}

bool parse_number_array(
    const json::Value& args,
    std::string_view name,
    std::size_t max_count,
    std::vector<double>* values_out,
    std::string* error_out) {
    const json::Value* value = json::find_member(args, name);
    if (value == nullptr || !value->is_array()) {
        *error_out = std::string(name) + " must be an array of numbers.";
        return false;
    }
    if (value->as_array().size() > max_count) {
        *error_out = std::string(name) + " exceeds the maximum allowed length.";
        return false;
    }
    std::vector<double> values;
    values.reserve(value->as_array().size());
    for (const json::Value& entry : value->as_array()) {
        if (!entry.is_number()) {
            *error_out = std::string(name) + " must be an array of numbers.";
            return false;
        }
        values.push_back(entry.as_number());
    }
    *values_out = std::move(values);
    return true;
}

std::optional<marrow::runtime::SlotColor> color_arg(
    const json::Value& args,
    std::string_view name,
    std::string* error_out) {
    const json::Value* value = json::find_member(args, name);
    if (value == nullptr || !value->is_object()) {
        *error_out = std::string(name) + " must be a color object.";
        return std::nullopt;
    }
    const auto r = number_arg(*value, "r");
    const auto g = number_arg(*value, "g");
    const auto b = number_arg(*value, "b");
    const auto a = number_arg(*value, "a");
    if (!r.has_value() || !g.has_value() || !b.has_value() || !a.has_value()) {
        *error_out = "color requires r, g, b, and a numbers.";
        return std::nullopt;
    }
    return marrow::runtime::SlotColor{*r, *g, *b, *a};
}

bool ensure_project_loaded(const EditorSession& session) {
    return session.has_project() && session.project() != nullptr &&
        session.runtime_data() != nullptr;
}

std::vector<std::string> names_from_indices(
    const std::vector<marrow::runtime::BoneData>& bones,
    const std::vector<std::size_t>& indices) {
    std::vector<std::string> names;
    names.reserve(indices.size());
    for (const std::size_t index : indices) {
        if (index < bones.size()) {
            names.push_back(bones[index].name);
        }
    }
    return names;
}

json::Value string_array_value(const std::vector<std::string>& values) {
    json::Value::Array array;
    array.reserve(values.size());
    for (const std::string& value : values) {
        array.push_back(string_value(value));
    }
    return array_value(std::move(array));
}

std::string attachment_kind_name(marrow::runtime::AttachmentKind kind) {
    switch (kind) {
    case marrow::runtime::AttachmentKind::Region:
        return "region";
    case marrow::runtime::AttachmentKind::Mesh:
        return "mesh";
    case marrow::runtime::AttachmentKind::LinkedMesh:
        return "linked_mesh";
    case marrow::runtime::AttachmentKind::Point:
        return "point";
    case marrow::runtime::AttachmentKind::BoundingBox:
        return "bounding_box";
    case marrow::runtime::AttachmentKind::Clipping:
        return "clipping";
    case marrow::runtime::AttachmentKind::Path:
        return "path";
    }
    return "unknown";
}

std::filesystem::path absolute_normalized(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::path absolute_path = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute_path = path;
    }
    return absolute_path.lexically_normal();
}

bool path_is_within(
    const std::filesystem::path& candidate,
    const std::filesystem::path& root) {
    const std::filesystem::path normalized_candidate = absolute_normalized(candidate);
    const std::filesystem::path normalized_root = absolute_normalized(root);
    auto root_it = normalized_root.begin();
    auto candidate_it = normalized_candidate.begin();
    for (; root_it != normalized_root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == normalized_candidate.end() || *root_it != *candidate_it) {
            return false;
        }
    }
    return true;
}

bool agent_path_allowed(
    const EditorSession& session,
    const std::filesystem::path& target_path) {
    if (!session.has_project() || session.project() == nullptr) {
        return false;
    }

    const std::filesystem::path project_dir =
        session.project()->source_path.empty()
            ? std::filesystem::current_path()
            : session.project()->source_path.parent_path();
    const std::filesystem::path export_dir =
        session.project()->resolved_export_skeleton_path().parent_path();

    return path_is_within(target_path, project_dir) ||
        path_is_within(target_path, export_dir) ||
        path_is_within(target_path, "/tmp") ||
        path_is_within(target_path, "/private/tmp");
}

json::Value review_to_json(const AgentReviewRequest& request) {
    json::Value::Object review;
    review.emplace("required", bool_value(true));
    review.emplace("id", number_value(static_cast<double>(request.id)));
    review.emplace(
        "kind",
        string_value(
            request.kind == AgentReviewKind::SaveProject
                ? "save"
                : request.kind == AgentReviewKind::ExportRuntime ? "export_runtime"
                                                                  : "import_or_pack"));
    review.emplace("op", string_value(request.op));
    review.emplace("label", string_value(request.label));
    review.emplace("target_path", string_value(request.target_path.string()));
    json::Value::Array targets;
    if (!request.target_paths.empty()) {
        targets.reserve(request.target_paths.size());
        for (const auto& target : request.target_paths) {
            targets.push_back(string_value(target.string()));
        }
    } else if (!request.target_path.empty()) {
        targets.push_back(string_value(request.target_path.string()));
    }
    review.emplace("targets", array_value(std::move(targets)));
    review.emplace("args_summary", string_value(request.args_summary));
    review.emplace("binary", bool_value(request.binary_output));
    review.emplace("allowed", bool_value(request.allowed));
    review.emplace("message", string_value(request.message));
    return object_value(std::move(review));
}

AgentDispatchResult enqueue_review(
    AgentCommandContext& context,
    std::string_view op,
    const OperationSpec* spec,
    AgentReviewKind kind,
    std::string label,
    std::filesystem::path target_path,
    bool binary_output,
    std::vector<std::filesystem::path> target_paths,
    std::string args_summary) {
    AgentReviewRequest request;
    request.id = context.control.next_review_id++;
    request.kind = kind;
    request.op = std::string(op);
    request.label = std::move(label);
    if (target_paths.empty() && !target_path.empty()) {
        target_paths.push_back(target_path);
    }
    request.target_paths.reserve(target_paths.size());
    for (const auto& path : target_paths) {
        request.target_paths.push_back(absolute_normalized(path));
    }
    request.target_path = request.target_paths.empty()
        ? absolute_normalized(target_path)
        : request.target_paths.front();
    request.binary_output = binary_output;
    request.args_summary = std::move(args_summary);
    request.allowed = !request.target_paths.empty();
    for (const auto& path : request.target_paths) {
        request.allowed = request.allowed && agent_path_allowed(context.session, path);
    }
    request.message = request.allowed
        ? "Waiting for editor approval."
        : "Rejected by path whitelist.";

    context.control.review_queue.push_back(request);

    AgentDispatchResult result = make_success(
        request.allowed ? "Agent request queued for review." : "Agent request requires review but target path is not allowed.",
        op,
        spec);
    result.requires_review = true;
    result.review = review_to_json(request);
    return result;
}

void append_activity(AgentControlState& control, AgentDispatchResult* result) {
    if (result == nullptr) {
        return;
    }
    result->activity_id = control.next_activity_id++;

    AgentActivityEntry entry;
    entry.id = result->activity_id;
    entry.op = result->op;
    entry.category = result->category;
    entry.ok = result->ok;
    entry.mutating = result->mutating;
    entry.requires_review = result->requires_review;
    entry.message = result->message;
    control.activity_log.push_back(std::move(entry));
    control.current_operation.clear();
    control.last_result = result->message;

    if (control.activity_log.size() > kMaxAgentActivityEntries) {
        const std::size_t overflow =
            control.activity_log.size() - kMaxAgentActivityEntries;
        control.activity_log.erase(
            control.activity_log.begin(),
            control.activity_log.begin() + static_cast<std::ptrdiff_t>(overflow));
    }
}

json::Value operation_specs_value() {
    json::Value::Array operations;
    operations.reserve(std::size(kOperationSpecs));
    for (const OperationSpec& spec : kOperationSpecs) {
        json::Value::Object object;
        object.emplace("name", string_value(std::string(spec.name)));
        object.emplace("category", string_value(std::string(spec.category)));
        object.emplace("mutating", bool_value(spec.mutating));
        object.emplace("requires_review", bool_value(spec.requires_review));
        object.emplace("dry_run_supported", bool_value(spec.dry_run_supported));
        operations.push_back(object_value(std::move(object)));
    }
    return array_value(std::move(operations));
}

json::Value slots_value(const marrow::runtime::SkeletonData& skeleton) {
    json::Value::Array slots;
    const auto& bones = skeleton.bones();
    const auto& source_slots = skeleton.slots();
    slots.reserve(source_slots.size());
    for (std::size_t index = 0; index < source_slots.size(); ++index) {
        const auto& slot = source_slots[index];
        json::Value::Object object;
        object.emplace("index", number_value(index));
        object.emplace("name", string_value(slot.name));
        object.emplace(
            "bone",
            string_value(slot.bone_index < bones.size() ? bones[slot.bone_index].name : ""));
        object.emplace("setup_attachment", string_value(slot.setup_attachment));
        slots.push_back(object_value(std::move(object)));
    }
    return array_value(std::move(slots));
}

json::Value skins_value(const marrow::runtime::SkeletonData& skeleton) {
    json::Value::Array skins;
    for (const auto& skin : skeleton.skins()) {
        json::Value::Object object;
        object.emplace("name", string_value(skin.name));
        object.emplace("attachment_count", number_value(skin.slot_attachments.size()));
        object.emplace("bone_count", number_value(skin.bone_indices.size()));
        object.emplace("ik_constraint_count", number_value(skin.ik_constraint_indices.size()));
        object.emplace("path_constraint_count", number_value(skin.path_constraint_indices.size()));
        object.emplace(
            "transform_constraint_count",
            number_value(skin.transform_constraint_indices.size()));
        object.emplace(
            "physics_constraint_count",
            number_value(skin.physics_constraint_indices.size()));
        skins.push_back(object_value(std::move(object)));
    }
    return array_value(std::move(skins));
}

json::Value attachment_object(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::SkinData& skin,
    const marrow::runtime::SkinSlotData& slot_attachment) {
    json::Value::Object object;
    const auto& attachment = slot_attachment.attachment;
    object.emplace("skin", string_value(skin.name));
    object.emplace(
        "slot",
        string_value(
            slot_attachment.slot_index < skeleton.slots().size()
                ? skeleton.slots()[slot_attachment.slot_index].name
                : ""));
    object.emplace("name", string_value(attachment.name));
    object.emplace("kind", string_value(attachment_kind_name(attachment.kind)));
    if (attachment.mesh_geometry != nullptr) {
        object.emplace(
            "vertex_count",
            number_value(attachment.mesh_geometry->vertices.size() / 2U));
        object.emplace(
            "triangle_count",
            number_value(attachment.mesh_geometry->triangles.size() / 3U));
    }
    if (attachment.linked_mesh.has_value()) {
        object.emplace("parent_attachment", string_value(attachment.linked_mesh->parent_attachment));
    }
    return object_value(std::move(object));
}

json::Value attachments_value(
    const marrow::runtime::SkeletonData& skeleton,
    const json::Value* args) {
    const std::optional<std::string_view> skin_filter =
        args != nullptr ? string_arg(*args, "skin") : std::nullopt;
    const std::optional<std::string_view> slot_filter =
        args != nullptr ? string_arg(*args, "slot") : std::nullopt;

    json::Value::Array attachments;
    for (const auto& skin : skeleton.skins()) {
        if (skin_filter.has_value() && skin.name != *skin_filter) {
            continue;
        }
        for (const auto& slot_attachment : skin.slot_attachments) {
            const std::string slot_name =
                slot_attachment.slot_index < skeleton.slots().size()
                    ? skeleton.slots()[slot_attachment.slot_index].name
                    : "";
            if (slot_filter.has_value() && slot_name != *slot_filter) {
                continue;
            }
            attachments.push_back(attachment_object(skeleton, skin, slot_attachment));
        }
    }
    return array_value(std::move(attachments));
}

json::Value constraints_value(const marrow::runtime::SkeletonData& skeleton) {
    json::Value::Array constraints;
    const auto& bones = skeleton.bones();
    for (const auto& constraint : skeleton.ik_constraints()) {
        json::Value::Object object;
        object.emplace("type", string_value("ik"));
        object.emplace("name", string_value(constraint.name));
        object.emplace("bones", string_array_value(names_from_indices(bones, constraint.bone_indices)));
        if (constraint.target_bone_index < bones.size()) {
            object.emplace("target", string_value(bones[constraint.target_bone_index].name));
        }
        constraints.push_back(object_value(std::move(object)));
    }
    for (const auto& constraint : skeleton.path_constraints()) {
        json::Value::Object object;
        object.emplace("type", string_value("path"));
        object.emplace("name", string_value(constraint.name));
        object.emplace("bones", string_array_value(names_from_indices(bones, constraint.bone_indices)));
        if (constraint.slot_index < skeleton.slots().size()) {
            object.emplace("slot", string_value(skeleton.slots()[constraint.slot_index].name));
        }
        constraints.push_back(object_value(std::move(object)));
    }
    for (const auto& constraint : skeleton.transform_constraints()) {
        json::Value::Object object;
        object.emplace("type", string_value("transform"));
        object.emplace("name", string_value(constraint.name));
        object.emplace(
            "bones",
            string_array_value(names_from_indices(bones, constraint.target_bone_indices)));
        if (constraint.source_bone_index < bones.size()) {
            object.emplace("source", string_value(bones[constraint.source_bone_index].name));
        }
        constraints.push_back(object_value(std::move(object)));
    }
    for (const auto& constraint : skeleton.physics_constraints()) {
        json::Value::Object object;
        object.emplace("type", string_value("physics"));
        object.emplace("name", string_value(constraint.name));
        object.emplace("bones", string_array_value(names_from_indices(bones, constraint.bone_indices)));
        constraints.push_back(object_value(std::move(object)));
    }
    return array_value(std::move(constraints));
}

std::optional<marrow::editor::DrawOrderTimelineEdit> draw_order_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view animation_name) {
    const marrow::runtime::AnimationData* animation = skeleton.find_animation(animation_name);
    if (animation == nullptr) {
        return std::nullopt;
    }

    marrow::editor::DrawOrderTimelineEdit edit;
    edit.animation_name = std::string(animation_name);
    const marrow::runtime::DrawOrderTimeline* timeline = animation->find_draw_order_timeline();
    if (timeline == nullptr) {
        return edit;
    }

    for (const auto& keyframe : timeline->keyframes) {
        marrow::editor::DrawOrderKeyframeEdit copied;
        copied.time = static_cast<double>(keyframe.time);
        copied.slot_names.reserve(keyframe.slot_indices.size());
        for (const std::size_t slot_index : keyframe.slot_indices) {
            if (slot_index >= skeleton.slots().size()) {
                return std::nullopt;
            }
            copied.slot_names.push_back(skeleton.slots()[slot_index].name);
        }
        edit.keyframes.push_back(std::move(copied));
    }
    return edit;
}

bool parse_complete_slot_order(
    const marrow::runtime::SkeletonData& skeleton,
    const json::Value& args,
    std::vector<std::string>* slots_out,
    std::string* error_out) {
    const json::Value* slots_value_arg = json::find_member(args, "slots");
    if (slots_value_arg == nullptr || !slots_value_arg->is_array()) {
        *error_out = "set_draw_order_keyframe requires slots array.";
        return false;
    }

    std::vector<std::string> slot_names;
    slot_names.reserve(slots_value_arg->as_array().size());
    std::set<std::string> seen;
    for (const json::Value& value : slots_value_arg->as_array()) {
        if (!value.is_string()) {
            *error_out = "draw-order slots must be strings.";
            return false;
        }
        const std::string& slot_name = value.as_string();
        if (!seen.insert(slot_name).second) {
            *error_out = "draw-order slots must not contain duplicates.";
            return false;
        }
        if (!skeleton.find_slot_index(slot_name).has_value()) {
            *error_out = "draw-order slot not found: " + slot_name;
            return false;
        }
        slot_names.push_back(slot_name);
    }

    if (slot_names.size() != skeleton.slots().size()) {
        *error_out = "draw-order slots must include every skeleton slot exactly once.";
        return false;
    }

    for (const auto& slot : skeleton.slots()) {
        if (seen.find(slot.name) == seen.end()) {
            *error_out = "draw-order slots missing skeleton slot: " + slot.name;
            return false;
        }
    }

    *slots_out = std::move(slot_names);
    return true;
}

const marrow::runtime::AttachmentData* find_mesh_attachment(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    std::optional<std::size_t>* slot_index_out) {
    const auto slot_index = skeleton.find_slot_index(slot_name);
    const auto* skin = skeleton.find_skin(skin_name);
    if (!slot_index.has_value() || skin == nullptr) {
        return nullptr;
    }
    if (slot_index_out != nullptr) {
        *slot_index_out = *slot_index;
    }
    const auto* attachment = skin->find_attachment(*slot_index, attachment_name);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        return nullptr;
    }
    return attachment;
}

marrow::editor::MeshWeightAttachmentEdit mesh_weight_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    const marrow::runtime::AttachmentData& attachment) {
    marrow::editor::MeshWeightAttachmentEdit edit;
    edit.skin_name = std::string(skin_name);
    edit.slot_name = std::string(slot_name);
    edit.attachment_name = std::string(attachment_name);
    if (attachment.mesh_geometry == nullptr) {
        return edit;
    }
    edit.vertices.reserve(attachment.mesh_geometry->weights.size());
    for (const auto& runtime_vertex : attachment.mesh_geometry->weights) {
        marrow::editor::MeshWeightVertexEdit vertex;
        vertex.influences.reserve(runtime_vertex.influences.size());
        for (const auto& influence : runtime_vertex.influences) {
            if (influence.bone_index >= skeleton.bones().size()) {
                continue;
            }
            vertex.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
                skeleton.bones()[influence.bone_index].name,
                influence.x,
                influence.y,
                influence.weight});
        }
        edit.vertices.push_back(std::move(vertex));
    }
    return edit;
}

void normalize_weight_vertex(marrow::editor::MeshWeightVertexEdit* vertex) {
    if (vertex == nullptr) {
        return;
    }
    double total = 0.0;
    for (const auto& influence : vertex->influences) {
        total += std::max(0.0, influence.weight);
    }
    if (total <= 0.0) {
        return;
    }
    for (auto& influence : vertex->influences) {
        influence.weight = std::max(0.0, influence.weight) / total;
    }
}

marrow::editor::MeshWeightAttachmentEdit* ensure_mesh_weight_edit(
    marrow::editor::ProjectData& project,
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    const marrow::runtime::AttachmentData& attachment) {
    if (auto* existing = project.find_mesh_weight_attachment_edit(
            skin_name, slot_name, attachment_name)) {
        return existing;
    }
    project.mesh_weight_attachment_edits.push_back(
        mesh_weight_edit_from_runtime(skeleton, skin_name, slot_name, attachment_name, attachment));
    return &project.mesh_weight_attachment_edits.back();
}

const marrow::runtime::PathConstraintData* find_runtime_path_constraint(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view name) {
    for (const auto& constraint : skeleton.path_constraints()) {
        if (constraint.name == name) {
            return &constraint;
        }
    }
    return nullptr;
}

const marrow::runtime::TransformConstraintData* find_runtime_transform_constraint(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view name) {
    for (const auto& constraint : skeleton.transform_constraints()) {
        if (constraint.name == name) {
            return &constraint;
        }
    }
    return nullptr;
}

const marrow::runtime::PhysicsConstraintData* find_runtime_physics_constraint(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view name) {
    for (const auto& constraint : skeleton.physics_constraints()) {
        if (constraint.name == name) {
            return &constraint;
        }
    }
    return nullptr;
}

PathConstraintEdit path_constraint_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::PathConstraintData& constraint) {
    PathConstraintEdit edit;
    edit.name = constraint.name;
    if (constraint.slot_index < skeleton.slots().size()) {
        edit.slot_name = skeleton.slots()[constraint.slot_index].name;
    }
    edit.bone_names = names_from_indices(skeleton.bones(), constraint.bone_indices);
    edit.position = constraint.position;
    edit.spacing = constraint.spacing;
    edit.spacing_mode = constraint.spacing_mode;
    edit.rotate_mix = constraint.rotate_mix;
    edit.translate_mix = constraint.translate_mix;
    return edit;
}

TransformConstraintEdit transform_constraint_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::TransformConstraintData& constraint) {
    TransformConstraintEdit edit;
    edit.name = constraint.name;
    if (constraint.source_bone_index < skeleton.bones().size()) {
        edit.source_bone_name = skeleton.bones()[constraint.source_bone_index].name;
    }
    edit.bone_names = names_from_indices(skeleton.bones(), constraint.target_bone_indices);
    edit.rotate_mix = constraint.rotate_mix;
    edit.translate_mix = constraint.translate_mix;
    edit.scale_mix = constraint.scale_mix;
    edit.shear_mix = constraint.shear_mix;
    edit.offsets = constraint.offsets;
    return edit;
}

PhysicsConstraintEdit physics_constraint_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::PhysicsConstraintData& constraint) {
    PhysicsConstraintEdit edit;
    edit.name = constraint.name;
    edit.bone_names = names_from_indices(skeleton.bones(), constraint.bone_indices);
    edit.step = constraint.step;
    edit.x = constraint.x;
    edit.y = constraint.y;
    edit.rotate = constraint.rotate;
    edit.scale_x = constraint.scale_x;
    edit.shear_x = constraint.shear_x;
    edit.limit = constraint.limit;
    edit.inertia = constraint.inertia;
    edit.damping = constraint.damping;
    edit.strength = constraint.strength;
    edit.mass_inverse = constraint.mass_inverse;
    edit.gravity = constraint.gravity;
    edit.wind = constraint.wind;
    edit.mix = constraint.mix;
    return edit;
}

std::string path_spacing_mode_name(marrow::runtime::PathConstraintSpacingMode mode) {
    switch (mode) {
    case marrow::runtime::PathConstraintSpacingMode::Length:
        return "length";
    case marrow::runtime::PathConstraintSpacingMode::Percent:
        return "percent";
    }
    return "length";
}

bool read_string_array_if_present(
    const json::Value& args,
    std::string_view primary,
    std::string_view fallback,
    std::vector<std::string>* values_out,
    std::string* error_out) {
    const json::Value* value = find_member_any(args, primary, fallback);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_array()) {
        *error_out = std::string(primary) + " must be an array of strings.";
        return false;
    }
    std::vector<std::string> values;
    values.reserve(value->as_array().size());
    std::set<std::string> seen;
    for (const json::Value& entry : value->as_array()) {
        if (!entry.is_string()) {
            *error_out = std::string(primary) + " must be an array of strings.";
            return false;
        }
        if (!seen.insert(entry.as_string()).second) {
            *error_out = std::string(primary) + " must not contain duplicate names.";
            return false;
        }
        values.push_back(entry.as_string());
    }
    *values_out = std::move(values);
    return true;
}

bool validate_bone_names(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& names,
    std::string_view field_name,
    std::string* error_out) {
    if (names.empty()) {
        *error_out = std::string(field_name) + " must target at least one bone.";
        return false;
    }
    std::set<std::string> seen;
    for (const std::string& name : names) {
        if (!seen.insert(name).second) {
            *error_out = std::string(field_name) + " must not contain duplicate bones.";
            return false;
        }
        if (!skeleton.find_bone_index(name).has_value()) {
            *error_out = "Bone not found: " + name;
            return false;
        }
    }
    return true;
}

bool apply_xy_if_present(
    const json::Value& args,
    std::string_view field_name,
    marrow::runtime::AttachmentVertex* target,
    std::string* error_out) {
    const json::Value* value = json::find_member(args, field_name);
    if (value == nullptr) {
        return true;
    }
    if (!value->is_object()) {
        *error_out = std::string(field_name) + " must be an object with x/y numbers.";
        return false;
    }
    double x = target->x;
    double y = target->y;
    if (!apply_number_if_present(*value, "x", {}, &x, error_out) ||
        !apply_number_if_present(*value, "y", {}, &y, error_out)) {
        return false;
    }
    target->x = static_cast<float>(x);
    target->y = static_cast<float>(y);
    return true;
}

bool apply_transform_offsets_if_present(
    const json::Value& args,
    marrow::runtime::TransformConstraintOffsets* offsets,
    std::string* error_out) {
    const json::Value* value = json::find_member(args, "offset");
    if (value == nullptr) {
        return true;
    }
    if (!value->is_object()) {
        *error_out = "offset must be an object.";
        return false;
    }
    return apply_number_if_present(*value, "rotation", {}, &offsets->rotation, error_out) &&
        apply_number_if_present(*value, "x", {}, &offsets->x, error_out) &&
        apply_number_if_present(*value, "y", {}, &offsets->y, error_out) &&
        apply_number_if_present(*value, "scale_x", "scaleX", &offsets->scale_x, error_out) &&
        apply_number_if_present(*value, "scale_y", "scaleY", &offsets->scale_y, error_out) &&
        apply_number_if_present(*value, "shear_x", "shearX", &offsets->shear_x, error_out) &&
        apply_number_if_present(*value, "shear_y", "shearY", &offsets->shear_y, error_out);
}

bool merge_path_constraint_args(
    const json::Value& args,
    const marrow::runtime::SkeletonData& skeleton,
    PathConstraintEdit* edit,
    std::string* error_out) {
    if (const auto slot = string_arg(args, "slot")) {
        if (!skeleton.find_slot_index(*slot).has_value()) {
            *error_out = "Slot not found: " + std::string(*slot);
            return false;
        }
        edit->slot_name = std::string(*slot);
    } else if (json::find_member(args, "slot") != nullptr) {
        *error_out = "slot must be a string.";
        return false;
    }
    if (!read_string_array_if_present(args, "bone_names", "bones", &edit->bone_names, error_out) ||
        !validate_bone_names(skeleton, edit->bone_names, "bone_names", error_out)) {
        return false;
    }

    if (const auto* spacing_mode_value = find_member_any(args, "spacing_mode", "spacingMode")) {
        if (!spacing_mode_value->is_string()) {
            *error_out = "spacing_mode must be 'length' or 'percent'.";
            return false;
        }
        if (spacing_mode_value->as_string() == "length") {
            edit->spacing_mode = marrow::runtime::PathConstraintSpacingMode::Length;
        } else if (spacing_mode_value->as_string() == "percent") {
            edit->spacing_mode = marrow::runtime::PathConstraintSpacingMode::Percent;
        } else {
            *error_out = "spacing_mode must be 'length' or 'percent'.";
            return false;
        }
    }

    if (!apply_number_if_present(args, "position", {}, &edit->position, error_out) ||
        !apply_number_if_present(args, "spacing", {}, &edit->spacing, error_out) ||
        !apply_number_if_present(args, "rotate_mix", "rotateMix", &edit->rotate_mix, error_out) ||
        !apply_number_if_present(args, "translate_mix", "translateMix", &edit->translate_mix, error_out)) {
        return false;
    }
    if (edit->position < 0.0 || edit->position > 1.0 ||
        edit->spacing < 0.0 ||
        edit->rotate_mix < 0.0 || edit->rotate_mix > 1.0 ||
        edit->translate_mix < 0.0 || edit->translate_mix > 1.0) {
        *error_out = "path constraint values are outside their valid ranges.";
        return false;
    }
    return true;
}

bool merge_transform_constraint_args(
    const json::Value& args,
    const marrow::runtime::SkeletonData& skeleton,
    TransformConstraintEdit* edit,
    std::string* error_out) {
    if (const auto source = string_arg(args, "source")) {
        if (!skeleton.find_bone_index(*source).has_value()) {
            *error_out = "Bone not found: " + std::string(*source);
            return false;
        }
        edit->source_bone_name = std::string(*source);
    } else if (json::find_member(args, "source") != nullptr) {
        *error_out = "source must be a string.";
        return false;
    }
    if (!read_string_array_if_present(args, "bone_names", "bones", &edit->bone_names, error_out) ||
        !validate_bone_names(skeleton, edit->bone_names, "bone_names", error_out)) {
        return false;
    }
    if (std::find(edit->bone_names.begin(), edit->bone_names.end(), edit->source_bone_name) !=
        edit->bone_names.end()) {
        *error_out = "transform constraint source bone must not also be a target.";
        return false;
    }
    if (!apply_number_if_present(args, "rotate_mix", "rotateMix", &edit->rotate_mix, error_out) ||
        !apply_number_if_present(args, "translate_mix", "translateMix", &edit->translate_mix, error_out) ||
        !apply_number_if_present(args, "scale_mix", "scaleMix", &edit->scale_mix, error_out) ||
        !apply_number_if_present(args, "shear_mix", "shearMix", &edit->shear_mix, error_out) ||
        !apply_transform_offsets_if_present(args, &edit->offsets, error_out)) {
        return false;
    }
    if (edit->rotate_mix < 0.0 || edit->rotate_mix > 1.0 ||
        edit->translate_mix < 0.0 || edit->translate_mix > 1.0 ||
        edit->scale_mix < 0.0 || edit->scale_mix > 1.0 ||
        edit->shear_mix < 0.0 || edit->shear_mix > 1.0) {
        *error_out = "transform constraint mix values must stay within [0, 1].";
        return false;
    }
    return true;
}

bool merge_physics_constraint_args(
    const json::Value& args,
    const marrow::runtime::SkeletonData& skeleton,
    PhysicsConstraintEdit* edit,
    std::string* error_out) {
    if (!read_string_array_if_present(args, "bone_names", "bones", &edit->bone_names, error_out) ||
        !validate_bone_names(skeleton, edit->bone_names, "bone_names", error_out)) {
        return false;
    }
    if (!apply_number_if_present(args, "step", {}, &edit->step, error_out) ||
        !apply_number_if_present(args, "x", {}, &edit->x, error_out) ||
        !apply_number_if_present(args, "y", {}, &edit->y, error_out) ||
        !apply_number_if_present(args, "rotate", {}, &edit->rotate, error_out) ||
        !apply_number_if_present(args, "scale_x", "scaleX", &edit->scale_x, error_out) ||
        !apply_number_if_present(args, "shear_x", "shearX", &edit->shear_x, error_out) ||
        !apply_number_if_present(args, "limit", {}, &edit->limit, error_out) ||
        !apply_number_if_present(args, "inertia", {}, &edit->inertia, error_out) ||
        !apply_number_if_present(args, "damping", {}, &edit->damping, error_out) ||
        !apply_number_if_present(args, "strength", {}, &edit->strength, error_out) ||
        !apply_number_if_present(args, "mass_inverse", "massInverse", &edit->mass_inverse, error_out) ||
        !apply_number_if_present(args, "mix", {}, &edit->mix, error_out) ||
        !apply_xy_if_present(args, "gravity", &edit->gravity, error_out) ||
        !apply_xy_if_present(args, "wind", &edit->wind, error_out)) {
        return false;
    }
    if (edit->step <= 0.0 ||
        edit->x < 0.0 || edit->y < 0.0 ||
        edit->rotate < 0.0 || edit->scale_x < 0.0 ||
        edit->shear_x < 0.0 || edit->limit < 0.0 ||
        edit->inertia < 0.0 || edit->inertia > 1.0 ||
        edit->damping < 0.0 || edit->strength < 0.0 ||
        edit->mass_inverse < 0.0 ||
        edit->mix < 0.0 || edit->mix > 1.0) {
        *error_out = "physics constraint values are outside their valid ranges.";
        return false;
    }
    return true;
}

json::Value xy_value(const marrow::runtime::AttachmentVertex& value) {
    json::Value::Object object;
    object.emplace("x", number_value(value.x));
    object.emplace("y", number_value(value.y));
    return object_value(std::move(object));
}

json::Value transform_offset_value(const marrow::runtime::TransformConstraintOffsets& offsets) {
    json::Value::Object object;
    object.emplace("rotation", number_value(offsets.rotation));
    object.emplace("x", number_value(offsets.x));
    object.emplace("y", number_value(offsets.y));
    object.emplace("scale_x", number_value(offsets.scale_x));
    object.emplace("scale_y", number_value(offsets.scale_y));
    object.emplace("shear_x", number_value(offsets.shear_x));
    object.emplace("shear_y", number_value(offsets.shear_y));
    return object_value(std::move(object));
}

json::Value path_constraint_preview_value(const PathConstraintEdit& edit, bool dry_run) {
    json::Value::Object object;
    object.emplace("dry_run", bool_value(dry_run));
    object.emplace("name", string_value(edit.name));
    object.emplace("slot", string_value(edit.slot_name));
    object.emplace("bones", string_array_value(edit.bone_names));
    object.emplace("position", number_value(edit.position));
    object.emplace("spacing", number_value(edit.spacing));
    object.emplace("spacing_mode", string_value(path_spacing_mode_name(edit.spacing_mode)));
    object.emplace("rotate_mix", number_value(edit.rotate_mix));
    object.emplace("translate_mix", number_value(edit.translate_mix));
    return object_value(std::move(object));
}

json::Value transform_constraint_preview_value(const TransformConstraintEdit& edit, bool dry_run) {
    json::Value::Object object;
    object.emplace("dry_run", bool_value(dry_run));
    object.emplace("name", string_value(edit.name));
    object.emplace("source", string_value(edit.source_bone_name));
    object.emplace("bones", string_array_value(edit.bone_names));
    object.emplace("rotate_mix", number_value(edit.rotate_mix));
    object.emplace("translate_mix", number_value(edit.translate_mix));
    object.emplace("scale_mix", number_value(edit.scale_mix));
    object.emplace("shear_mix", number_value(edit.shear_mix));
    object.emplace("offset", transform_offset_value(edit.offsets));
    return object_value(std::move(object));
}

json::Value physics_constraint_preview_value(const PhysicsConstraintEdit& edit, bool dry_run) {
    json::Value::Object object;
    object.emplace("dry_run", bool_value(dry_run));
    object.emplace("name", string_value(edit.name));
    object.emplace("bones", string_array_value(edit.bone_names));
    object.emplace("step", number_value(edit.step));
    object.emplace("x", number_value(edit.x));
    object.emplace("y", number_value(edit.y));
    object.emplace("rotate", number_value(edit.rotate));
    object.emplace("scale_x", number_value(edit.scale_x));
    object.emplace("shear_x", number_value(edit.shear_x));
    object.emplace("limit", number_value(edit.limit));
    object.emplace("inertia", number_value(edit.inertia));
    object.emplace("damping", number_value(edit.damping));
    object.emplace("strength", number_value(edit.strength));
    object.emplace("mass_inverse", number_value(edit.mass_inverse));
    object.emplace("gravity", xy_value(edit.gravity));
    object.emplace("wind", xy_value(edit.wind));
    object.emplace("mix", number_value(edit.mix));
    return object_value(std::move(object));
}

json::Value timeline_description_value(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::editor::ProjectData& project,
    std::string_view animation_name) {
    const auto* animation = skeleton.find_animation(animation_name);
    json::Value::Object object;
    object.emplace("animation", string_value(std::string(animation_name)));
    if (animation == nullptr) {
        object.emplace("exists", bool_value(false));
        return object_value(std::move(object));
    }

    object.emplace("exists", bool_value(true));
    object.emplace("duration", number_value(animation->duration()));
    object.emplace("bone_rotate_timelines", number_value(animation->bone_rotate_timelines.size()));
    object.emplace("bone_translate_timelines", number_value(animation->bone_translate_timelines.size()));
    object.emplace("bone_scale_timelines", number_value(animation->bone_scale_timelines.size()));
    object.emplace("bone_shear_timelines", number_value(animation->bone_shear_timelines.size()));
    object.emplace("slot_attachment_timelines", number_value(animation->slot_attachment_timelines.size()));
    object.emplace("slot_color_timelines", number_value(animation->slot_color_timelines.size()));
    object.emplace("mesh_deform_timelines", number_value(animation->mesh_deform_timelines.size()));
    const auto* runtime_draw_order = animation->find_draw_order_timeline();
    const auto* project_draw_order = project.find_draw_order_timeline_edit(animation_name);
    object.emplace(
        "draw_order_keyframes",
        number_value(
            project_draw_order != nullptr
                ? project_draw_order->keyframes.size()
                : (runtime_draw_order != nullptr ? runtime_draw_order->keyframes.size() : 0U)));
    object.emplace("event_timeline", bool_value(animation->find_event_timeline() != nullptr));
    return object_value(std::move(object));
}

} // namespace agent_detail

using namespace agent_detail;

AgentDispatchResult AgentCommandDispatcher::dispatch(
    AgentCommandContext& context,
    const json::Value& cmd) {
    AgentDispatchResult result;
    const json::Value* op_value =
        cmd.is_object() ? json::find_member(cmd, "op") : nullptr;
    const OperationSpec* spec = nullptr;

    if (!cmd.is_object()) {
        result = make_error("Command must be an object.");
    } else if (op_value == nullptr || !op_value->is_string()) {
        result = make_error("Command must have a string 'op' field.");
    } else {
        const std::string_view op = op_value->as_string();
        spec = find_operation(op);
        if (spec == nullptr) {
            result = make_error(
                "Unknown operation: " + std::string(op),
                op,
                nullptr,
                "unknown_operation");
        } else if (context.control.paused && !op_allowed_while_paused(*spec)) {
            result = make_error(
                "Agent is paused; mutating operation blocked.",
                op,
                spec,
                "blocked");
        } else {
            if (spec->requires_project) {
                context.control.current_operation = std::string(op);
            }
            if (spec->requires_project && !ensure_project_loaded(context.session)) {
                result = make_error(
                    "No project loaded.",
                    op,
                    spec,
                    "project_not_loaded");
            } else if (spec->handler == nullptr) {
                result = make_error(
                    "Operation has no registered handler: " + std::string(op),
                    op,
                    spec,
                    "unknown_operation");
            } else {
                result = spec->handler(context, cmd, *spec);
            }
        }
    }
    append_activity(context.control, &result);
    return result;
}

json::Value AgentCommandDispatcher::result_to_json(AgentDispatchResult result) {
    json::Value::Object result_obj;
    result_obj.emplace("ok", json::Value(result.ok, {}));
    result_obj.emplace("message", json::Value(std::move(result.message), {}));
    result_obj.emplace("scene_delta", std::move(result.scene_delta));
    if (!result.op.empty()) {
        result_obj.emplace("op", json::Value(std::move(result.op), {}));
    }
    if (!result.category.empty()) {
        result_obj.emplace("category", json::Value(std::move(result.category), {}));
    }
    result_obj.emplace("mutating", json::Value(result.mutating, {}));
    if (!result.error_code.empty()) {
        json::Value::Object error;
        error.emplace("code", json::Value(std::move(result.error_code), {}));
        result_obj.emplace("error", json::Value(std::move(error), {}));
    }
    if (result.requires_review || !result.review.is_null()) {
        result_obj.emplace("review", std::move(result.review));
    }
    if (result.activity_id != 0U) {
        result_obj.emplace("activity_id", json::Value(static_cast<double>(result.activity_id), {}));
    }
    return json::Value(std::move(result_obj), {});
}

const AgentOperationDescriptor* agent_operation_descriptors() noexcept {
    static const std::vector<AgentOperationDescriptor> descriptors = [] {
        std::vector<AgentOperationDescriptor> result;
        result.reserve(std::size(kOperationSpecs));
        for (const OperationSpec& spec : kOperationSpecs) {
            result.push_back({
                spec.name,
                spec.category,
                spec.mutating,
                spec.requires_review,
                spec.dry_run_supported,
                spec.handler != nullptr});
        }
        return result;
    }();
    return descriptors.data();
}

std::size_t agent_operation_descriptor_count() noexcept {
    return std::size(kOperationSpecs);
}

bool validate_agent_operation_registry(std::string* error_out) {
    std::set<std::string_view> names;
    for (const OperationSpec& spec : kOperationSpecs) {
        if (spec.name.empty()) {
            if (error_out != nullptr) {
                *error_out = "Agent operation registry contains an empty name.";
            }
            return false;
        }
        if (spec.handler == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Agent operation has no handler: " + std::string(spec.name);
            }
            return false;
        }
        if (!names.insert(spec.name).second) {
            if (error_out != nullptr) {
                *error_out = "Duplicate agent operation: " + std::string(spec.name);
            }
            return false;
        }
    }
    if (error_out != nullptr) {
        error_out->clear();
    }
    return true;
}

} // namespace marrow::editor
