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

#include "shell_types.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/runtime/animation_compare.hpp"

namespace marrow::editor::shell {

namespace json = marrow::runtime::json;

namespace {

constexpr double kKeyTimeEpsilon = 1e-6;
constexpr std::size_t kMaxAgentActivityEntries = 200;

struct OperationSpec {
    std::string_view name;
    std::string_view category;
    bool mutating{false};
    bool requires_review{false};
    bool dry_run_supported{false};
};

constexpr OperationSpec kOperationSpecs[] = {
    {"operations.list", "inspection", false, false},
    {"scene.describe", "inspection", false, false},
    {"bones.list", "inspection", false, false},
    {"animation.list", "inspection", false, false},
    {"slots.list", "inspection", false, false},
    {"skins.list", "inspection", false, false},
    {"attachments.list", "inspection", false, false},
    {"constraints.list", "inspection", false, false},
    {"timeline.describe", "inspection", false, false},
    {"mesh.describe", "inspection", false, false},
    {"project.diagnostics", "inspection", false, false},
    {"export.preview", "validation", false, false},
    {"runtime.validate", "validation", false, false},
    {"compare_runtime_export", "validation", false, false},
    {"agent.permissions.describe", "management", false, false},
    {"agent.pause", "management", false, false},
    {"agent.resume", "management", false, false},
    {"agent.terminate", "management", false, false},
    {"undo", "edit", true, false},
    {"redo", "edit", true, false},
    {"set_transform", "edit", true, false, true},
    {"remove_transform_keyframe", "edit", true, false},
    {"set_event_keyframe", "edit", true, false, true},
    {"remove_event_keyframe", "edit", true, false},
    {"set_deform_keyframe", "edit", true, false, true},
    {"remove_deform_keyframe", "edit", true, false},
    {"set_vertex_weights", "edit", true, false, true},
    {"normalize_weights", "edit", true, false, true},
    {"edit_ik_constraint", "edit", true, false, true},
    {"edit_path_constraint", "edit", true, false, true},
    {"edit_transform_constraint", "edit", true, false, true},
    {"edit_physics_constraint", "edit", true, false, true},
    {"set_slot_color_keyframe", "edit", true, false, true},
    {"remove_slot_color_keyframe", "edit", true, false},
    {"set_attachment_keyframe", "edit", true, false, true},
    {"remove_attachment_keyframe", "edit", true, false},
    {"set_draw_order_keyframe", "edit", true, false, true},
    {"remove_draw_order_keyframe", "edit", true, false},
    {"save", "management", true, true},
    {"export_runtime", "management", true, true},
    {"import.spine_json", "management", true, true, true},
    {"import.spine_atlas", "management", true, true, true},
    {"import.psd_layers", "management", true, true, true},
    {"atlas.pack", "management", true, true, true},
};

const OperationSpec* find_operation(std::string_view op) {
    for (const OperationSpec& spec : kOperationSpecs) {
        if (spec.name == op) {
            return &spec;
        }
    }
    return nullptr;
}

bool op_allowed_while_paused(std::string_view op, const OperationSpec& spec) {
    return !spec.mutating ||
        op == "agent.resume" ||
        op == "agent.pause" ||
        op == "agent.permissions.describe" ||
        op == "operations.list";
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
    std::string_view op = {},
    const OperationSpec* spec = nullptr,
    std::string error_code = "invalid_request") {
    return make_result(false, std::move(message), op, spec, json::Value(), std::move(error_code));
}

AgentDispatchResult make_error_with_delta(
    std::string message,
    std::string_view op,
    const OperationSpec* spec,
    json::Value delta,
    std::string error_code = "invalid_request") {
    return make_result(false, std::move(message), op, spec, std::move(delta), std::move(error_code));
}

AgentDispatchResult make_success(
    std::string message,
    std::string_view op,
    const OperationSpec* spec,
    json::Value delta = json::Value()) {
    return make_result(true, std::move(message), op, spec, std::move(delta));
}

const json::Value* command_args(const json::Value& cmd) {
    const json::Value* args = json::find_member(cmd, "args");
    return args != nullptr && args->is_object() ? args : nullptr;
}

bool bool_arg(const json::Value* args, std::string_view name, bool default_value = false) {
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
    std::string_view fallback = {}) {
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

bool ensure_project_loaded(ShellState* state) {
    return state != nullptr && state->load_result &&
        state->load_result.project != nullptr &&
        state->load_result.skeleton_data != nullptr;
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
    const ShellState& state,
    const std::filesystem::path& target_path) {
    if (!state.load_result || state.load_result.project == nullptr) {
        return false;
    }

    const std::filesystem::path project_dir =
        state.project_path.empty() ? std::filesystem::current_path()
                                   : state.project_path.parent_path();
    const std::filesystem::path export_dir =
        state.load_result.project->resolved_export_skeleton_path().parent_path();

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
    ShellState* state,
    std::string_view op,
    const OperationSpec* spec,
    AgentReviewKind kind,
    std::string label,
    std::filesystem::path target_path,
    bool binary_output,
    std::vector<std::filesystem::path> target_paths = {},
    std::string args_summary = {}) {
    AgentReviewRequest request;
    request.id = state->next_agent_review_id++;
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
        request.allowed = request.allowed && agent_path_allowed(*state, path);
    }
    request.message = request.allowed
        ? "Waiting for editor approval."
        : "Rejected by path whitelist.";

    state->agent_review_queue.push_back(request);

    AgentDispatchResult result = make_success(
        request.allowed ? "Agent request queued for review." : "Agent request requires review but target path is not allowed.",
        op,
        spec);
    result.requires_review = true;
    result.review = review_to_json(request);
    return result;
}

void append_activity(ShellState* state, AgentDispatchResult* result) {
    if (state == nullptr || result == nullptr) {
        return;
    }
    result->activity_id = state->next_agent_activity_id++;

    AgentActivityEntry entry;
    entry.id = result->activity_id;
    entry.op = result->op;
    entry.category = result->category;
    entry.ok = result->ok;
    entry.mutating = result->mutating;
    entry.requires_review = result->requires_review;
    entry.message = result->message;
    state->agent_activity_log.push_back(std::move(entry));
    state->agent_current_op.clear();
    state->agent_last_result = result->message;

    if (state->agent_activity_log.size() > kMaxAgentActivityEntries) {
        const std::size_t overflow =
            state->agent_activity_log.size() - kMaxAgentActivityEntries;
        state->agent_activity_log.erase(
            state->agent_activity_log.begin(),
            state->agent_activity_log.begin() + static_cast<std::ptrdiff_t>(overflow));
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
    std::optional<std::size_t>* slot_index_out = nullptr) {
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

AgentDispatchResult dispatch_impl(ShellState* state, const json::Value& cmd) {
    if (state == nullptr) {
        return make_error("Shell state is null.");
    }

    if (!cmd.is_object()) {
        return make_error("Command must be an object.");
    }

    const json::Value* op_val = json::find_member(cmd, "op");
    if (op_val == nullptr || !op_val->is_string()) {
        return make_error("Command must have a string 'op' field.");
    }

    const std::string_view op = op_val->as_string();
    const OperationSpec* spec = find_operation(op);
    if (spec == nullptr) {
        return make_error("Unknown operation: " + std::string(op), op, nullptr, "unknown_operation");
    }

    if (op == "operations.list") {
        return make_success("Operations listed", op, spec, operation_specs_value());
    }

    if (op == "agent.permissions.describe") {
        json::Value::Object permissions;
        permissions.emplace("paused", bool_value(state->agent_paused));
        permissions.emplace("terminated", bool_value(state->agent_terminated));
        permissions.emplace("local_only", bool_value(true));
        permissions.emplace("review_required_for_file_writes", bool_value(true));
        permissions.emplace("current_op", string_value(state->agent_current_op));
        permissions.emplace("last_result", string_value(state->agent_last_result));
        permissions.emplace("pending_reviews", number_value(state->agent_review_queue.size()));
        return make_success("Agent permissions described.", op, spec, object_value(std::move(permissions)));
    }

    if (op == "agent.pause") {
        state->agent_paused = true;
        return make_success("Agent paused.", op, spec);
    }

    if (op == "agent.resume") {
        state->agent_paused = false;
        state->agent_terminated = false;
        return make_success("Agent resumed.", op, spec);
    }

    if (op == "agent.terminate") {
        state->agent_terminated = true;
        state->agent_paused = true;
        state->agent_current_op.clear();
        return make_success("Agent session terminated.", op, spec);
    }

    if (state->agent_paused && !op_allowed_while_paused(op, *spec)) {
        return make_error(
            "Agent is paused; mutating operation blocked.",
            op,
            spec,
            "blocked");
    }
    state->agent_current_op = std::string(op);

    if (!ensure_project_loaded(state)) {
        return make_error("No project loaded.", op, spec, "project_not_loaded");
    }

    auto& project = *state->load_result.project;
    const auto& skeleton = *state->load_result.skeleton_data;

    if (op == "undo") {
        std::string label;
        if (state->command_stack.undo(state, &label)) {
            update_project_dirty_state(state);
            state->status_message = "Undone: " + label;
            return make_success(state->status_message, op, spec);
        }
        return make_error("Nothing to undo.", op, spec, "nothing_to_undo");
    }

    if (op == "redo") {
        std::string label;
        if (state->command_stack.redo(state, &label)) {
            update_project_dirty_state(state);
            state->status_message = "Redone: " + label;
            return make_success(state->status_message, op, spec);
        }
        return make_error("Nothing to redo.", op, spec, "nothing_to_redo");
    }

    if (op == "save") {
        return enqueue_review(
            state,
            op,
            spec,
            AgentReviewKind::SaveProject,
            "Save project",
            state->project_path,
            false);
    }

    if (op == "export_runtime") {
        const json::Value* args = command_args(cmd);
        const bool binary_output = bool_arg(args, "binary");
        return enqueue_review(
            state,
            op,
            spec,
            AgentReviewKind::ExportRuntime,
            "Export runtime assets",
            project.resolved_export_skeleton_path(),
            binary_output);
    }

    if (op == "export.preview") {
        const json::Value* args = command_args(cmd);
        const bool binary_output = bool_arg(args, "binary");
        json::Value::Object preview;
        preview.emplace("binary", bool_value(binary_output));
        preview.emplace("requires_review", bool_value(true));
        json::Value::Array targets;
        targets.push_back(string_value(project.resolved_export_skeleton_path().string()));
        if (binary_output) {
            targets.push_back(string_value(project.resolved_export_binary_path().string()));
        }
        for (const auto& atlas_path : project.resolved_atlas_paths()) {
            targets.push_back(string_value(atlas_path.string()));
        }
        preview.emplace("targets", array_value(std::move(targets)));
        return make_success("Export preview generated.", op, spec, object_value(std::move(preview)));
    }

    if (op == "import.spine_json" ||
        op == "import.spine_atlas" ||
        op == "import.psd_layers" ||
        op == "atlas.pack") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(std::string(op) + " requires 'args' object.", op, spec);
        }

        const bool dry_run = bool_arg(args, "dry_run", true);
        std::optional<std::filesystem::path> input_path;
        std::vector<std::filesystem::path> targets;
        std::string label;
        if (op == "import.spine_json") {
            input_path = path_arg_any(*args, {"input", "json_path", "path"});
            targets.push_back(
                path_arg_any(*args, {"output", "skeleton_output", "target"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_spine_import.mskl")));
            label = "Import Spine JSON";
        } else if (op == "import.spine_atlas") {
            input_path = path_arg_any(*args, {"input", "atlas_path", "path"});
            targets.push_back(
                path_arg_any(*args, {"output", "atlas_output", "target"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_spine_import.matl")));
            label = "Import Spine atlas";
        } else if (op == "import.psd_layers") {
            input_path = path_arg_any(*args, {"input", "psd_path", "path"});
            targets.push_back(
                path_arg_any(*args, {"output", "skeleton_output", "target"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_psd_layers.mskl")));
            targets.push_back(
                path_arg_any(*args, {"atlas_output"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_psd_layers.matl")));
            label = "Import PSD layers";
        } else {
            targets.push_back(
                path_arg_any(*args, {"output", "atlas_output", "atlas_path", "target"})
                    .value_or(std::filesystem::path("/tmp/marrow_agent_atlas_pack.matl")));
            label = "Pack atlas";
        }

        if (op != "atlas.pack" && !input_path.has_value()) {
            return make_error(std::string(op) + " requires an input path.", op, spec);
        }
        if (input_path.has_value() && !agent_path_allowed(*state, *input_path)) {
            return make_error("Input path is outside the agent whitelist.", op, spec, "forbidden_path");
        }
        for (const auto& target : targets) {
            if (!agent_path_allowed(*state, target)) {
                return make_error("Output path is outside the agent whitelist.", op, spec, "forbidden_path");
            }
        }

        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(dry_run));
        preview.emplace("requires_review", bool_value(true));
        if (input_path.has_value()) {
            preview.emplace("input", string_value(absolute_normalized(*input_path).string()));
        }
        json::Value::Array target_values;
        target_values.reserve(targets.size());
        for (const auto& target : targets) {
            target_values.push_back(string_value(absolute_normalized(target).string()));
        }
        preview.emplace("targets", array_value(std::move(target_values)));
        if (dry_run) {
            return make_success(label + " dry-run validated.", op, spec, object_value(std::move(preview)));
        }

        std::string summary = "op=" + std::string(op);
        if (input_path.has_value()) {
            summary += " input=" + absolute_normalized(*input_path).string();
        }
        if (!targets.empty()) {
            summary += " targets=";
            for (std::size_t index = 0; index < targets.size(); ++index) {
                if (index > 0U) {
                    summary += ",";
                }
                summary += absolute_normalized(targets[index]).string();
            }
        }
        return enqueue_review(
            state,
            op,
            spec,
            AgentReviewKind::ImportOrPack,
            std::move(label),
            targets.empty() ? std::filesystem::path() : targets.front(),
            false,
            std::move(targets),
            std::move(summary));
    }

    if (op == "runtime.validate") {
        if (state->load_result.base_skeleton_document == nullptr) {
            json::Value::Object payload;
            json::Value::Object diagnostics;
            diagnostics.emplace("error_count", number_value(std::size_t{1}));
            diagnostics.emplace("warning_count", number_value(std::size_t{0}));
            diagnostics.emplace("message", string_value("No base skeleton document is loaded."));
            payload.emplace("diagnostics", object_value(std::move(diagnostics)));
            return make_error_with_delta(
                "Runtime validation failed.",
                op,
                spec,
                object_value(std::move(payload)),
                "validation_failed");
        }
        const auto runtime_result = marrow::editor::build_project_runtime(
            project,
            *state->load_result.base_skeleton_document);
        json::Value::Object payload;
        json::Value::Object diagnostics;
        diagnostics.emplace(
            "error_count",
            number_value(static_cast<std::size_t>(runtime_result ? 0U : 1U)));
        diagnostics.emplace("warning_count", number_value(std::size_t{0}));
        if (runtime_result) {
            diagnostics.emplace("bone_count", number_value(runtime_result.skeleton_data->bones().size()));
            diagnostics.emplace("slot_count", number_value(runtime_result.skeleton_data->slots().size()));
            payload.emplace("diagnostics", object_value(std::move(diagnostics)));
            return make_success("Runtime validation passed.", op, spec, object_value(std::move(payload)));
        }
        diagnostics.emplace(
            "message",
            string_value(runtime_result.error.has_value()
                             ? runtime_result.error->format()
                             : std::string("Unknown runtime validation failure.")));
        payload.emplace("diagnostics", object_value(std::move(diagnostics)));
        return make_error_with_delta(
            "Runtime validation failed.",
            op,
            spec,
            object_value(std::move(payload)),
            "validation_failed");
    }

    if (op == "compare_runtime_export") {
        if (state->load_result.base_skeleton_document == nullptr) {
            return make_error("No base skeleton document is loaded.", op, spec, "validation_failed");
        }
        const json::Value* args = command_args(cmd);
        const bool binary_output = bool_arg(args, "binary", true);
        ProjectExportOptions options;
        options.skeleton_output_path = std::filesystem::path("/tmp/marrow_agent_compare_runtime.mskl");
        if (binary_output) {
            options.binary_output_path = std::filesystem::path("/tmp/marrow_agent_compare_runtime.mbin");
        }
        const auto export_result = marrow::editor::export_runtime_assets(
            project,
            *state->load_result.base_skeleton_document,
            options);
        if (!export_result) {
            return make_error(
                "Runtime comparison export failed: " +
                    (export_result.error.has_value()
                         ? export_result.error->format()
                         : std::string("unknown export failure")),
                op,
                spec,
                "export_failed");
        }

        const auto json_runtime = marrow::runtime::load_skeleton_data(export_result.path);
        if (!json_runtime) {
            return make_error(json_runtime.error->format(), op, spec, "validation_failed");
        }

        json::Value::Object summary;
        summary.emplace("json_path", string_value(export_result.path.string()));
        summary.emplace("binary", bool_value(binary_output));
        summary.emplace("bone_count", number_value(json_runtime.skeleton_data->bones().size()));
        summary.emplace("slot_count", number_value(json_runtime.skeleton_data->slots().size()));
        std::error_code file_error;
        const auto json_size = std::filesystem::file_size(export_result.path, file_error);
        if (!file_error) {
            summary.emplace("json_bytes", number_value(static_cast<std::size_t>(json_size)));
        }

        if (binary_output) {
            if (!export_result.binary_path.has_value()) {
                return make_error("Binary comparison export did not produce a binary path.", op, spec);
            }
            const auto binary_runtime = marrow::runtime::load_skeleton_data(*export_result.binary_path);
            if (!binary_runtime) {
                return make_error(binary_runtime.error->format(), op, spec, "validation_failed");
            }
            const auto comparison = marrow::runtime::compare_animation_roundtrip(
                *json_runtime.skeleton_data,
                *binary_runtime.skeleton_data);
            if (!comparison) {
                return make_error(
                    "Animation comparison failed: " + *comparison.error,
                    op,
                    spec,
                    "validation_failed");
            }
            summary.emplace("binary_path", string_value(export_result.binary_path->string()));
            file_error.clear();
            const auto binary_size = std::filesystem::file_size(*export_result.binary_path, file_error);
            if (!file_error) {
                summary.emplace("binary_bytes", number_value(static_cast<std::size_t>(binary_size)));
            }
            summary.emplace(
                "rotation_error_degrees",
                number_value(comparison.metrics.max_rotation_error_degrees));
            summary.emplace(
                "position_error_pixels",
                number_value(comparison.metrics.max_translation_error_pixels));
            summary.emplace(
                "rotate_keyframes",
                number_value(comparison.metrics.roundtrip_rotation_keyframes));
        } else {
            summary.emplace("rotation_error_degrees", number_value(0.0));
            summary.emplace("position_error_pixels", number_value(0.0));
        }

        return make_success("Runtime export comparison passed.", op, spec, object_value(std::move(summary)));
    }

    if (op == "scene.describe") {
        json::Value::Object scene_desc;
        scene_desc.emplace("path", string_value(state->project_path.string()));
        scene_desc.emplace("name", string_value(project.editor_metadata.name));
        scene_desc.emplace("export_directory", string_value(project.editor_metadata.export_directory.string()));
        scene_desc.emplace("bone_count", number_value(skeleton.bones().size()));
        scene_desc.emplace("slot_count", number_value(skeleton.slots().size()));
        scene_desc.emplace("skin_count", number_value(skeleton.skins().size()));
        scene_desc.emplace("animation_count", number_value(skeleton.animations().size()));
        scene_desc.emplace("ik_constraint_count", number_value(skeleton.ik_constraints().size()));
        scene_desc.emplace("path_constraint_count", number_value(skeleton.path_constraints().size()));
        scene_desc.emplace(
            "transform_constraint_count",
            number_value(skeleton.transform_constraints().size()));
        scene_desc.emplace(
            "physics_constraint_count",
            number_value(skeleton.physics_constraints().size()));
        scene_desc.emplace("project_dirty", bool_value(state->project_dirty));
        return make_success("Scene described", op, spec, object_value(std::move(scene_desc)));
    }

    if (op == "bones.list") {
        json::Value::Array bones_arr;
        for (const auto& bone : skeleton.bones()) {
            bones_arr.push_back(string_value(bone.name));
        }
        return make_success("Bones listed", op, spec, array_value(std::move(bones_arr)));
    }

    if (op == "animation.list") {
        json::Value::Array anim_arr;
        for (const auto& anim : skeleton.animations()) {
            anim_arr.push_back(string_value(anim.name));
        }
        return make_success("Animations listed", op, spec, array_value(std::move(anim_arr)));
    }

    if (op == "slots.list") {
        return make_success("Slots listed", op, spec, slots_value(skeleton));
    }

    if (op == "skins.list") {
        return make_success("Skins listed", op, spec, skins_value(skeleton));
    }

    if (op == "attachments.list") {
        return make_success(
            "Attachments listed",
            op,
            spec,
            attachments_value(skeleton, command_args(cmd)));
    }

    if (op == "constraints.list") {
        return make_success("Constraints listed", op, spec, constraints_value(skeleton));
    }

    if (op == "timeline.describe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("timeline.describe requires args object.", op, spec);
        }
        const auto animation_name = string_arg(*args, "animation");
        if (!animation_name.has_value()) {
            return make_error("timeline.describe requires animation string.", op, spec);
        }
        return make_success(
            "Timeline described",
            op,
            spec,
            timeline_description_value(skeleton, project, *animation_name));
    }

    if (op == "mesh.describe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("mesh.describe requires args object.", op, spec);
        }
        const auto skin_name = string_arg(*args, "skin");
        const auto slot_name = string_arg(*args, "slot");
        const auto attachment_name = string_arg(*args, "attachment");
        if (!skin_name.has_value() || !slot_name.has_value() || !attachment_name.has_value()) {
            return make_error(
                "mesh.describe requires skin, slot, and attachment strings.",
                op,
                spec);
        }
        const auto slot_index = skeleton.find_slot_index(*slot_name);
        const auto* skin = skeleton.find_skin(*skin_name);
        if (!slot_index.has_value() || skin == nullptr) {
            return make_error("mesh.describe target skin or slot not found.", op, spec, "not_found");
        }
        const auto* attachment = skin->find_attachment(*slot_index, *attachment_name);
        if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
            return make_error("mesh.describe target mesh not found.", op, spec, "not_found");
        }
        json::Value::Object mesh;
        mesh.emplace("skin", string_value(std::string(*skin_name)));
        mesh.emplace("slot", string_value(std::string(*slot_name)));
        mesh.emplace("attachment", string_value(std::string(*attachment_name)));
        mesh.emplace("kind", string_value(attachment_kind_name(attachment->kind)));
        mesh.emplace("vertex_count", number_value(attachment->mesh_geometry->vertices.size() / 2U));
        mesh.emplace("triangle_count", number_value(attachment->mesh_geometry->triangles.size() / 3U));
        mesh.emplace("weighted_vertex_count", number_value(attachment->mesh_geometry->weights.size()));
        return make_success("Mesh described", op, spec, object_value(std::move(mesh)));
    }

    if (op == "project.diagnostics") {
        json::Value::Object diagnostics;
        diagnostics.emplace("error_count", number_value(std::size_t{0}));
        diagnostics.emplace(
            "warning_count",
            number_value(static_cast<std::size_t>(state->project_dirty ? 1U : 0U)));
        diagnostics.emplace("project_dirty", bool_value(state->project_dirty));
        diagnostics.emplace("review_queue_count", number_value(state->agent_review_queue.size()));
        return make_success(
            "Project diagnostics reported",
            op,
            spec,
            object_value(std::move(diagnostics)));
    }

    if (op == "set_transform") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_transform requires 'args' object.", op, spec);
        }

        const auto anim_name = string_arg(*args, "animation");
        const auto bone_name = string_arg(*args, "bone");
        const auto channel_str = string_arg(*args, "channel");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !bone_name.has_value() ||
            !channel_str.has_value() || !time.has_value()) {
            return make_error(
                "set_transform requires animation(str), bone(str), channel(str), time(num).",
                op,
                spec);
        }

        TransformTimelineChannel channel;
        if (*channel_str == "rotate") {
            channel = TransformTimelineChannel::Rotate;
        } else if (*channel_str == "translate") {
            channel = TransformTimelineChannel::Translate;
        } else if (*channel_str == "scale") {
            channel = TransformTimelineChannel::Scale;
        } else if (*channel_str == "shear") {
            channel = TransformTimelineChannel::Shear;
        } else {
            return make_error(
                "Invalid channel. Must be rotate, translate, scale, or shear.",
                op,
                spec);
        }

        if (!skeleton.find_bone_index(*bone_name).has_value()) {
            return make_error("Bone not found: " + std::string(*bone_name), op, spec, "not_found");
        }

        if (bool_arg(args, "dry_run")) {
            json::Value::Object preview;
            preview.emplace("dry_run", bool_value(true));
            preview.emplace("animation", string_value(std::string(*anim_name)));
            preview.emplace("bone", string_value(std::string(*bone_name)));
            preview.emplace("channel", string_value(std::string(*channel_str)));
            preview.emplace("time", number_value(*time));
            if (channel == TransformTimelineChannel::Rotate) {
                const auto angle = number_arg(*args, "angle");
                if (!angle.has_value()) {
                    return make_error("rotate channel requires 'angle' number.", op, spec);
                }
                preview.emplace("angle", number_value(*angle));
            } else {
                preview.emplace("x", number_value(number_arg(*args, "x").value_or(0.0)));
                preview.emplace("y", number_value(number_arg(*args, "y").value_or(0.0)));
            }
            return make_success("Transform keyframe validated.", op, spec, object_value(std::move(preview)));
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);

        TransformTimelineEdit* edit =
            project.find_transform_timeline_edit(*anim_name, *bone_name, channel);
        if (edit == nullptr) {
            project.transform_timeline_edits.push_back({
                std::string(*anim_name),
                std::string(*bone_name),
                channel,
                {},
            });
            edit = &project.transform_timeline_edits.back();
        }

        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const TransformKeyframeEdit& keyframe, double key_time) {
                return keyframe.time < key_time;
            });

        TransformKeyframeEdit* key = nullptr;
        if (key_it != edit->keyframes.end() && std::abs(key_it->time - *time) < kKeyTimeEpsilon) {
            key = &(*key_it);
        } else {
            key_it = edit->keyframes.insert(key_it, TransformKeyframeEdit{});
            key = &(*key_it);
            key->time = *time;
            key->interpolation = marrow::runtime::Interpolation::linear();
        }

        if (channel == TransformTimelineChannel::Rotate) {
            const auto angle = number_arg(*args, "angle");
            if (!angle.has_value()) {
                return make_error("rotate channel requires 'angle' number.", op, spec);
            }
            key->angle = *angle;
        } else {
            if (const auto x = number_arg(*args, "x")) {
                key->x = *x;
            }
            if (const auto y = number_arg(*args, "y")) {
                key->y = *y;
            }
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply transform: " + state->error_message, op, spec);
        }

        if (!record_action_from_snapshots(
                state,
                before,
                EditActionKind::AddKeyframe,
                "Set transform keyframe via Agent",
                "Agent",
                bool_arg(args, "merge"))) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        return make_success("Set transform keyframe successfully.", op, spec);
    }

    if (op == "remove_transform_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("remove_transform_keyframe requires 'args' object.", op, spec);
        }

        const auto anim_name = string_arg(*args, "animation");
        const auto bone_name = string_arg(*args, "bone");
        const auto channel_str = string_arg(*args, "channel");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !bone_name.has_value() ||
            !channel_str.has_value() || !time.has_value()) {
            return make_error(
                "remove_transform_keyframe requires animation, bone, channel, time.",
                op,
                spec);
        }

        TransformTimelineChannel channel;
        if (*channel_str == "rotate") {
            channel = TransformTimelineChannel::Rotate;
        } else if (*channel_str == "translate") {
            channel = TransformTimelineChannel::Translate;
        } else if (*channel_str == "scale") {
            channel = TransformTimelineChannel::Scale;
        } else if (*channel_str == "shear") {
            channel = TransformTimelineChannel::Shear;
        } else {
            return make_error("Invalid channel.", op, spec);
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        TransformTimelineEdit* edit =
            project.find_transform_timeline_edit(*anim_name, *bone_name, channel);
        if (edit == nullptr) {
            return make_error("Timeline edit not found.", op, spec, "not_found");
        }

        auto key_it = std::find_if(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            [&](const TransformKeyframeEdit& keyframe) {
                return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit->keyframes.end()) {
            return make_error("Keyframe not found at that time.", op, spec, "not_found");
        }

        edit->keyframes.erase(key_it);

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to rebuild runtime: " + state->error_message, op, spec);
        }

        if (!record_action_from_snapshots(
                state,
                before,
                EditActionKind::RemoveKeyframe,
                "Remove transform keyframe via Agent",
                "Agent",
                false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        return make_success("Removed transform keyframe successfully.", op, spec);
    }

    if (op == "edit_ik_constraint") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("edit_ik_constraint requires 'args' object.", op, spec);
        }

        const auto name = string_arg(*args, "name");
        if (!name.has_value()) {
            return make_error("edit_ik_constraint requires 'name' string.", op, spec);
        }

        if (bool_arg(args, "dry_run")) {
            const IkConstraintEdit* project_edit = project.find_ik_constraint_edit(*name);
            const marrow::runtime::IkConstraintData* runtime_constraint = nullptr;
            for (const auto& constraint : skeleton.ik_constraints()) {
                if (constraint.name == *name) {
                    runtime_constraint = &constraint;
                    break;
                }
            }
            if (project_edit == nullptr && runtime_constraint == nullptr) {
                return make_error("IK constraint not found in runtime skeleton.", op, spec, "not_found");
            }
            json::Value::Object preview;
            preview.emplace("dry_run", bool_value(true));
            preview.emplace("name", string_value(std::string(*name)));
            if (const auto mix = number_arg(*args, "mix")) {
                preview.emplace("mix", number_value(*mix));
            } else if (project_edit != nullptr) {
                preview.emplace("mix", number_value(project_edit->mix));
            } else {
                preview.emplace("mix", number_value(runtime_constraint->mix));
            }
            return make_success("IK constraint edit validated.", op, spec, object_value(std::move(preview)));
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        IkConstraintEdit* edit = project.find_ik_constraint_edit(*name);
        if (edit == nullptr) {
            const marrow::runtime::IkConstraintData* runtime_constraint = nullptr;
            for (const auto& constraint : skeleton.ik_constraints()) {
                if (constraint.name == *name) {
                    runtime_constraint = &constraint;
                    break;
                }
            }
            if (runtime_constraint == nullptr) {
                return make_error("IK constraint not found in runtime skeleton.", op, spec, "not_found");
            }

            IkConstraintEdit new_edit;
            new_edit.name = std::string(*name);
            new_edit.bone_names =
                names_from_indices(skeleton.bones(), runtime_constraint->bone_indices);
            if (runtime_constraint->target_bone_index < skeleton.bones().size()) {
                new_edit.target_bone_name =
                    skeleton.bones()[runtime_constraint->target_bone_index].name;
            }
            new_edit.mix = runtime_constraint->mix;
            new_edit.bend_positive = runtime_constraint->bend_positive;
            new_edit.softness = runtime_constraint->softness;
            new_edit.compress = runtime_constraint->compress;
            new_edit.stretch = runtime_constraint->stretch;

            project.ik_constraint_edits.push_back(std::move(new_edit));
            edit = &project.ik_constraint_edits.back();
        }

        if (const json::Value* target_val = json::find_member(*args, "target")) {
            if (target_val->is_string()) {
                edit->target_bone_name = target_val->as_string();
            } else if (!target_val->is_null()) {
                return make_error("target must be string or null.", op, spec);
            }
        }

        if (const json::Value* bones_val = json::find_member(*args, "bone_names")) {
            if (!bones_val->is_array()) {
                return make_error("bone_names must be an array of strings.", op, spec);
            }
            edit->bone_names.clear();
            for (const auto& bone : bones_val->as_array()) {
                if (!bone.is_string()) {
                    return make_error("bone_names must be an array of strings.", op, spec);
                }
                edit->bone_names.push_back(bone.as_string());
            }
        }

        if (const json::Value* mix_val = json::find_member(*args, "mix")) {
            if (mix_val->is_number()) {
                edit->mix = mix_val->as_number();
            } else if (!mix_val->is_null()) {
                return make_error("mix must be number or null.", op, spec);
            }
        }

        if (const json::Value* bend_val = json::find_member(*args, "bend_positive")) {
            if (bend_val->is_boolean()) {
                edit->bend_positive = bend_val->as_boolean();
            } else if (!bend_val->is_null()) {
                return make_error("bend_positive must be bool or null.", op, spec);
            }
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error(
                "Failed to apply IK constraint edit: " + state->error_message,
                op,
                spec);
        }

        if (!record_action_from_snapshots(
                state,
                before,
                EditActionKind::EditProperty,
                "Edit IK Constraint via Agent",
                "Agent",
                bool_arg(args, "merge"))) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        return make_success("Edited IK constraint successfully.", op, spec);
    }

    if (op == "edit_path_constraint") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("edit_path_constraint requires 'args' object.", op, spec);
        }
        const auto name = string_arg(*args, "name");
        if (!name.has_value()) {
            return make_error("edit_path_constraint requires 'name' string.", op, spec);
        }

        PathConstraintEdit merged;
        if (const PathConstraintEdit* existing = project.find_path_constraint_edit(*name)) {
            merged = *existing;
        } else if (const auto* runtime_constraint = find_runtime_path_constraint(skeleton, *name)) {
            merged = path_constraint_edit_from_runtime(skeleton, *runtime_constraint);
        } else {
            return make_error("Path constraint not found.", op, spec, "not_found");
        }

        std::string merge_error;
        if (!merge_path_constraint_args(*args, skeleton, &merged, &merge_error)) {
            return make_error(std::move(merge_error), op, spec);
        }
        if (bool_arg(args, "dry_run")) {
            return make_success(
                "Path constraint edit validated.",
                op,
                spec,
                path_constraint_preview_value(merged, true));
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        if (PathConstraintEdit* edit = project.find_path_constraint_edit(*name)) {
            *edit = std::move(merged);
        } else {
            project.path_constraint_edits.push_back(std::move(merged));
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply path constraint edit: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state,
                before,
                EditActionKind::EditProperty,
                "Edit path constraint via Agent",
                "Agent",
                bool_arg(args, "merge"))) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success(
            "Edited path constraint successfully.",
            op,
            spec,
            path_constraint_preview_value(*project.find_path_constraint_edit(*name), false));
    }

    if (op == "edit_transform_constraint") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("edit_transform_constraint requires 'args' object.", op, spec);
        }
        const auto name = string_arg(*args, "name");
        if (!name.has_value()) {
            return make_error("edit_transform_constraint requires 'name' string.", op, spec);
        }

        TransformConstraintEdit merged;
        if (const TransformConstraintEdit* existing = project.find_transform_constraint_edit(*name)) {
            merged = *existing;
        } else if (const auto* runtime_constraint = find_runtime_transform_constraint(skeleton, *name)) {
            merged = transform_constraint_edit_from_runtime(skeleton, *runtime_constraint);
        } else {
            return make_error("Transform constraint not found.", op, spec, "not_found");
        }

        std::string merge_error;
        if (!merge_transform_constraint_args(*args, skeleton, &merged, &merge_error)) {
            return make_error(std::move(merge_error), op, spec);
        }
        if (bool_arg(args, "dry_run")) {
            return make_success(
                "Transform constraint edit validated.",
                op,
                spec,
                transform_constraint_preview_value(merged, true));
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        if (TransformConstraintEdit* edit = project.find_transform_constraint_edit(*name)) {
            *edit = std::move(merged);
        } else {
            project.transform_constraint_edits.push_back(std::move(merged));
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply transform constraint edit: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state,
                before,
                EditActionKind::EditProperty,
                "Edit transform constraint via Agent",
                "Agent",
                bool_arg(args, "merge"))) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success(
            "Edited transform constraint successfully.",
            op,
            spec,
            transform_constraint_preview_value(*project.find_transform_constraint_edit(*name), false));
    }

    if (op == "edit_physics_constraint") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("edit_physics_constraint requires 'args' object.", op, spec);
        }
        const auto name = string_arg(*args, "name");
        if (!name.has_value()) {
            return make_error("edit_physics_constraint requires 'name' string.", op, spec);
        }

        PhysicsConstraintEdit merged;
        if (const PhysicsConstraintEdit* existing = project.find_physics_constraint_edit(*name)) {
            merged = *existing;
        } else if (const auto* runtime_constraint = find_runtime_physics_constraint(skeleton, *name)) {
            merged = physics_constraint_edit_from_runtime(skeleton, *runtime_constraint);
        } else {
            return make_error("Physics constraint not found.", op, spec, "not_found");
        }

        std::string merge_error;
        if (!merge_physics_constraint_args(*args, skeleton, &merged, &merge_error)) {
            return make_error(std::move(merge_error), op, spec);
        }
        if (bool_arg(args, "dry_run")) {
            return make_success(
                "Physics constraint edit validated.",
                op,
                spec,
                physics_constraint_preview_value(merged, true));
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        if (PhysicsConstraintEdit* edit = project.find_physics_constraint_edit(*name)) {
            *edit = std::move(merged);
        } else {
            project.physics_constraint_edits.push_back(std::move(merged));
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply physics constraint edit: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state,
                before,
                EditActionKind::EditProperty,
                "Edit physics constraint via Agent",
                "Agent",
                bool_arg(args, "merge"))) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success(
            "Edited physics constraint successfully.",
            op,
            spec,
            physics_constraint_preview_value(*project.find_physics_constraint_edit(*name), false));
    }

    if (op == "set_event_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_event_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto event_name = string_arg(*args, "event");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !event_name.has_value() || !time.has_value()) {
            return make_error("set_event_keyframe requires animation, event, and time.", op, spec);
        }
        if (skeleton.find_animation(*anim_name) == nullptr) {
            return make_error("Animation not found: " + std::string(*anim_name), op, spec, "not_found");
        }
        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(bool_arg(args, "dry_run")));
        preview.emplace("animation", string_value(std::string(*anim_name)));
        preview.emplace("event", string_value(std::string(*event_name)));
        preview.emplace("time", number_value(*time));
        if (bool_arg(args, "dry_run")) {
            return make_success("Event keyframe validated.", op, spec, object_value(std::move(preview)));
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        EventTimelineEdit* edit = project.find_event_timeline_edit(*anim_name);
        if (edit == nullptr) {
            project.event_timeline_edits.push_back({std::string(*anim_name), {}});
            edit = &project.event_timeline_edits.back();
        }
        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const EventKeyframeEdit& keyframe, double key_time) {
                return keyframe.time < key_time;
            });
        if (key_it == edit->keyframes.end() || std::abs(key_it->time - *time) >= kKeyTimeEpsilon ||
            key_it->event_name != *event_name) {
            key_it = edit->keyframes.insert(key_it, EventKeyframeEdit{});
        }
        key_it->time = *time;
        key_it->event_name = std::string(*event_name);
        if (const auto value = integer_arg(*args, "int")) {
            key_it->int_value = *value;
        }
        if (const auto value = number_arg(*args, "float")) {
            key_it->float_value = *value;
        }
        if (const auto value = string_arg(*args, "string")) {
            key_it->string_value = std::string(*value);
        }
        if (const auto value = string_arg(*args, "audio_path")) {
            key_it->audio_path = std::string(*value);
        }
        if (const auto value = number_arg(*args, "volume")) {
            key_it->volume = *value;
        }
        if (const auto value = number_arg(*args, "balance")) {
            key_it->balance = *value;
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply event keyframe: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state, before, EditActionKind::AddKeyframe, "Set event keyframe via Agent", "Agent", false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set event keyframe successfully.", op, spec, object_value(std::move(preview)));
    }

    if (op == "remove_event_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("remove_event_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto event_name = string_arg(*args, "event");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !event_name.has_value() || !time.has_value()) {
            return make_error("remove_event_keyframe requires animation, event, and time.", op, spec);
        }
        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        auto edit_it = std::find_if(
            project.event_timeline_edits.begin(),
            project.event_timeline_edits.end(),
            [&](const EventTimelineEdit& edit) { return edit.animation_name == *anim_name; });
        if (edit_it == project.event_timeline_edits.end()) {
            return make_error("Event timeline edit not found.", op, spec, "not_found");
        }
        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const EventKeyframeEdit& keyframe) {
                return keyframe.event_name == *event_name &&
                    std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            return make_error("Event keyframe not found.", op, spec, "not_found");
        }
        edit_it->keyframes.erase(key_it);
        if (edit_it->keyframes.empty()) {
            project.event_timeline_edits.erase(edit_it);
        }
        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to remove event keyframe: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state, before, EditActionKind::RemoveKeyframe, "Remove event keyframe via Agent", "Agent", false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Removed event keyframe successfully.", op, spec);
    }

    if (op == "set_deform_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_deform_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto attachment_name = string_arg(*args, "attachment");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() ||
            !attachment_name.has_value() || !time.has_value()) {
            return make_error("set_deform_keyframe requires animation, slot, attachment, and time.", op, spec);
        }
        if (skeleton.find_animation(*anim_name) == nullptr) {
            return make_error("Animation not found: " + std::string(*anim_name), op, spec, "not_found");
        }
        const auto slot_index = skeleton.find_slot_index(*slot_name);
        const auto* attachment = slot_index.has_value()
            ? skeleton.find_attachment_source(*slot_index, *attachment_name)
            : nullptr;
        if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
            return make_error("Mesh attachment not found.", op, spec, "not_found");
        }
        std::vector<double> offsets;
        std::string parse_error;
        if (!parse_number_array(*args, "offsets", 65536U, &offsets, &parse_error)) {
            return make_error(std::move(parse_error), op, spec);
        }
        if (offsets.size() != attachment->mesh_geometry->vertices.size()) {
            return make_error("offsets must match the target mesh vertex offset count.", op, spec);
        }
        std::string interpolation_error;
        const auto interpolation = interpolation_arg(*args, "interpolation", &interpolation_error);
        if (!interpolation.has_value()) {
            return make_error(std::move(interpolation_error), op, spec);
        }
        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(bool_arg(args, "dry_run")));
        preview.emplace("offset_count", number_value(offsets.size()));
        if (bool_arg(args, "dry_run")) {
            return make_success("Deform keyframe validated.", op, spec, object_value(std::move(preview)));
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        MeshDeformTimelineEdit* edit =
            project.find_mesh_deform_timeline_edit(*anim_name, *slot_name, *attachment_name);
        if (edit == nullptr) {
            project.mesh_deform_timeline_edits.push_back(
                {std::string(*anim_name), std::string(*slot_name), std::string(*attachment_name), {}});
            edit = &project.mesh_deform_timeline_edits.back();
        }
        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const DeformKeyframeEdit& keyframe, double key_time) { return keyframe.time < key_time; });
        if (key_it == edit->keyframes.end() || std::abs(key_it->time - *time) >= kKeyTimeEpsilon) {
            key_it = edit->keyframes.insert(key_it, DeformKeyframeEdit{});
        }
        key_it->time = *time;
        key_it->vertex_offsets = std::move(offsets);
        key_it->interpolation = *interpolation;
        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply deform keyframe: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state, before, EditActionKind::AddKeyframe, "Set deform keyframe via Agent", "Agent", false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set deform keyframe successfully.", op, spec, object_value(std::move(preview)));
    }

    if (op == "remove_deform_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("remove_deform_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto attachment_name = string_arg(*args, "attachment");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() ||
            !attachment_name.has_value() || !time.has_value()) {
            return make_error("remove_deform_keyframe requires animation, slot, attachment, and time.", op, spec);
        }
        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        auto edit_it = std::find_if(
            project.mesh_deform_timeline_edits.begin(),
            project.mesh_deform_timeline_edits.end(),
            [&](const MeshDeformTimelineEdit& edit) {
                return edit.animation_name == *anim_name &&
                    edit.slot_name == *slot_name &&
                    edit.attachment_name == *attachment_name;
            });
        if (edit_it == project.mesh_deform_timeline_edits.end()) {
            return make_error("Deform timeline edit not found.", op, spec, "not_found");
        }
        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const DeformKeyframeEdit& keyframe) {
                return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            return make_error("Deform keyframe not found.", op, spec, "not_found");
        }
        edit_it->keyframes.erase(key_it);
        if (edit_it->keyframes.empty()) {
            project.mesh_deform_timeline_edits.erase(edit_it);
        }
        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to remove deform keyframe: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state, before, EditActionKind::RemoveKeyframe, "Remove deform keyframe via Agent", "Agent", false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Removed deform keyframe successfully.", op, spec);
    }

    if (op == "set_vertex_weights" || op == "normalize_weights") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(std::string(op) + " requires 'args' object.", op, spec);
        }
        const auto skin_name = string_arg(*args, "skin");
        const auto slot_name = string_arg(*args, "slot");
        const auto attachment_name = string_arg(*args, "attachment");
        if (!skin_name.has_value() || !slot_name.has_value() || !attachment_name.has_value()) {
            return make_error(std::string(op) + " requires skin, slot, and attachment.", op, spec);
        }
        const auto* attachment =
            find_mesh_attachment(skeleton, *skin_name, *slot_name, *attachment_name);
        if (attachment == nullptr) {
            return make_error("Mesh attachment not found.", op, spec, "not_found");
        }
        if (bool_arg(args, "dry_run")) {
            json::Value::Object preview;
            preview.emplace("dry_run", bool_value(true));
            preview.emplace("vertex_count", number_value(attachment->mesh_geometry->weights.size()));
            return make_success("Mesh weight edit validated.", op, spec, object_value(std::move(preview)));
        }
        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        MeshWeightAttachmentEdit* edit = ensure_mesh_weight_edit(
            project, skeleton, *skin_name, *slot_name, *attachment_name, *attachment);

        if (op == "set_vertex_weights") {
            const json::Value* vertices = json::find_member(*args, "vertices");
            if (vertices == nullptr || !vertices->is_array()) {
                return make_error("set_vertex_weights requires vertices array.", op, spec);
            }
            for (const json::Value& vertex_value : vertices->as_array()) {
                if (!vertex_value.is_object()) {
                    return make_error("vertices entries must be objects.", op, spec);
                }
                const auto index_number = number_arg(vertex_value, "index");
                if (!index_number.has_value() || *index_number < 0.0 ||
                    std::abs(*index_number - std::round(*index_number)) > 1e-6) {
                    return make_error("vertex index must be a non-negative integer.", op, spec);
                }
                const std::size_t vertex_index = static_cast<std::size_t>(std::round(*index_number));
                if (vertex_index >= edit->vertices.size()) {
                    return make_error("vertex index is outside the target mesh.", op, spec);
                }
                const json::Value* influences = json::find_member(vertex_value, "influences");
                if (influences == nullptr || !influences->is_array() ||
                    influences->as_array().empty() || influences->as_array().size() > 4U) {
                    return make_error("vertex influences must contain 1 to 4 entries.", op, spec);
                }
                MeshWeightVertexEdit next_vertex;
                for (const json::Value& influence_value : influences->as_array()) {
                    if (!influence_value.is_object()) {
                        return make_error("influences entries must be objects.", op, spec);
                    }
                    const auto bone_name = string_arg(influence_value, "bone");
                    const auto x = number_arg(influence_value, "x");
                    const auto y = number_arg(influence_value, "y");
                    const auto weight = number_arg(influence_value, "weight");
                    if (!bone_name.has_value() || !x.has_value() || !y.has_value() ||
                        !weight.has_value()) {
                        return make_error("influences require bone, x, y, and weight.", op, spec);
                    }
                    if (!skeleton.find_bone_index(*bone_name).has_value()) {
                        return make_error("Bone not found: " + std::string(*bone_name), op, spec, "not_found");
                    }
                    next_vertex.influences.push_back(
                        MeshWeightInfluenceEdit{std::string(*bone_name), *x, *y, *weight});
                }
                if (bool_arg(args, "normalize", true)) {
                    normalize_weight_vertex(&next_vertex);
                }
                edit->vertices[vertex_index] = std::move(next_vertex);
            }
        } else {
            for (auto& vertex : edit->vertices) {
                normalize_weight_vertex(&vertex);
            }
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply mesh weights: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state, before, EditActionKind::EditProperty, "Edit mesh weights via Agent", "Agent", false)) {
            if (op == "normalize_weights") {
                return make_success("Mesh weights already normalized.", op, spec);
            }
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Edited mesh weights successfully.", op, spec);
    }

    if (op == "set_slot_color_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_slot_color_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() || !time.has_value()) {
            return make_error("set_slot_color_keyframe requires animation, slot, and time.", op, spec);
        }
        if (skeleton.find_animation(*anim_name) == nullptr ||
            !skeleton.find_slot_index(*slot_name).has_value()) {
            return make_error("Animation or slot not found.", op, spec, "not_found");
        }
        std::string color_error;
        const auto color = color_arg(*args, "color", &color_error);
        if (!color.has_value()) {
            return make_error(std::move(color_error), op, spec);
        }
        std::string interpolation_error;
        const auto interpolation = interpolation_arg(*args, "interpolation", &interpolation_error);
        if (!interpolation.has_value()) {
            return make_error(std::move(interpolation_error), op, spec);
        }
        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(bool_arg(args, "dry_run")));
        preview.emplace("slot", string_value(std::string(*slot_name)));
        if (bool_arg(args, "dry_run")) {
            return make_success("Slot color keyframe validated.", op, spec, object_value(std::move(preview)));
        }
        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        SlotColorTimelineEdit* edit = project.find_slot_color_timeline_edit(*anim_name, *slot_name);
        if (edit == nullptr) {
            project.slot_color_timeline_edits.push_back({std::string(*anim_name), std::string(*slot_name), {}});
            edit = &project.slot_color_timeline_edits.back();
        }
        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const SlotColorKeyframeEdit& keyframe, double key_time) { return keyframe.time < key_time; });
        if (key_it == edit->keyframes.end() || std::abs(key_it->time - *time) >= kKeyTimeEpsilon) {
            key_it = edit->keyframes.insert(key_it, SlotColorKeyframeEdit{});
        }
        key_it->time = *time;
        key_it->color = *color;
        key_it->interpolation = *interpolation;
        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply slot color: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state, before, EditActionKind::AddKeyframe, "Set slot color keyframe via Agent", "Agent", false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set slot color keyframe successfully.", op, spec, object_value(std::move(preview)));
    }

    if (op == "remove_slot_color_keyframe" || op == "remove_attachment_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(std::string(op) + " requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() || !time.has_value()) {
            return make_error(std::string(op) + " requires animation, slot, and time.", op, spec);
        }
        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        bool removed = false;
        if (op == "remove_slot_color_keyframe") {
            auto edit_it = std::find_if(
                project.slot_color_timeline_edits.begin(),
                project.slot_color_timeline_edits.end(),
                [&](const SlotColorTimelineEdit& edit) {
                    return edit.animation_name == *anim_name && edit.slot_name == *slot_name;
                });
            if (edit_it != project.slot_color_timeline_edits.end()) {
                auto key_it = std::find_if(
                    edit_it->keyframes.begin(),
                    edit_it->keyframes.end(),
                    [&](const SlotColorKeyframeEdit& keyframe) {
                        return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
                    });
                if (key_it != edit_it->keyframes.end()) {
                    edit_it->keyframes.erase(key_it);
                    removed = true;
                    if (edit_it->keyframes.empty()) {
                        project.slot_color_timeline_edits.erase(edit_it);
                    }
                }
            }
        } else {
            auto edit_it = std::find_if(
                project.slot_attachment_timeline_edits.begin(),
                project.slot_attachment_timeline_edits.end(),
                [&](const SlotAttachmentTimelineEdit& edit) {
                    return edit.animation_name == *anim_name && edit.slot_name == *slot_name;
                });
            if (edit_it != project.slot_attachment_timeline_edits.end()) {
                auto key_it = std::find_if(
                    edit_it->keyframes.begin(),
                    edit_it->keyframes.end(),
                    [&](const SlotAttachmentKeyframeEdit& keyframe) {
                        return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
                    });
                if (key_it != edit_it->keyframes.end()) {
                    edit_it->keyframes.erase(key_it);
                    removed = true;
                    if (edit_it->keyframes.empty()) {
                        project.slot_attachment_timeline_edits.erase(edit_it);
                    }
                }
            }
        }
        if (!removed) {
            return make_error("Slot keyframe not found.", op, spec, "not_found");
        }
        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to remove slot keyframe: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state, before, EditActionKind::RemoveKeyframe, "Remove slot keyframe via Agent", "Agent", false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Removed slot keyframe successfully.", op, spec);
    }

    if (op == "set_attachment_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_attachment_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() || !time.has_value()) {
            return make_error("set_attachment_keyframe requires animation, slot, and time.", op, spec);
        }
        const auto slot_index = skeleton.find_slot_index(*slot_name);
        if (skeleton.find_animation(*anim_name) == nullptr || !slot_index.has_value()) {
            return make_error("Animation or slot not found.", op, spec, "not_found");
        }
        std::optional<std::string> attachment_name;
        const json::Value* attachment_value = json::find_member(*args, "attachment");
        if (attachment_value == nullptr) {
            return make_error("set_attachment_keyframe requires attachment string or null.", op, spec);
        }
        if (attachment_value->is_string()) {
            attachment_name = attachment_value->as_string();
            if (skeleton.find_attachment_source(*slot_index, *attachment_name) == nullptr) {
                return make_error("Attachment not found for slot.", op, spec, "not_found");
            }
        } else if (!attachment_value->is_null()) {
            return make_error("attachment must be string or null.", op, spec);
        }
        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(bool_arg(args, "dry_run")));
        if (attachment_name.has_value()) {
            preview.emplace("attachment", string_value(*attachment_name));
        }
        if (bool_arg(args, "dry_run")) {
            return make_success("Attachment keyframe validated.", op, spec, object_value(std::move(preview)));
        }
        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        SlotAttachmentTimelineEdit* edit =
            project.find_slot_attachment_timeline_edit(*anim_name, *slot_name);
        if (edit == nullptr) {
            project.slot_attachment_timeline_edits.push_back({std::string(*anim_name), std::string(*slot_name), {}});
            edit = &project.slot_attachment_timeline_edits.back();
        }
        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const SlotAttachmentKeyframeEdit& keyframe, double key_time) { return keyframe.time < key_time; });
        if (key_it == edit->keyframes.end() || std::abs(key_it->time - *time) >= kKeyTimeEpsilon) {
            key_it = edit->keyframes.insert(key_it, SlotAttachmentKeyframeEdit{});
        }
        key_it->time = *time;
        key_it->attachment_name = std::move(attachment_name);
        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply attachment keyframe: " + state->error_message, op, spec);
        }
        if (!record_action_from_snapshots(
                state, before, EditActionKind::AddKeyframe, "Set attachment keyframe via Agent", "Agent", false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set attachment keyframe successfully.", op, spec, object_value(std::move(preview)));
    }

    if (op == "set_draw_order_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_draw_order_keyframe requires 'args' object.", op, spec);
        }

        const auto anim_name = string_arg(*args, "animation");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !time.has_value()) {
            return make_error(
                "set_draw_order_keyframe requires animation string and time number.",
                op,
                spec);
        }
        if (skeleton.find_animation(*anim_name) == nullptr) {
            return make_error("Animation not found: " + std::string(*anim_name), op, spec, "not_found");
        }

        std::vector<std::string> slot_order;
        std::string parse_error;
        if (!parse_complete_slot_order(skeleton, *args, &slot_order, &parse_error)) {
            return make_error(std::move(parse_error), op, spec);
        }

        json::Value::Object preview;
        preview.emplace("animation", string_value(std::string(*anim_name)));
        preview.emplace("time", number_value(*time));
        preview.emplace("slots", string_array_value(slot_order));
        if (bool_arg(args, "dry_run")) {
            return make_success(
                "Draw-order keyframe validated.",
                op,
                spec,
                object_value(std::move(preview)));
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        DrawOrderTimelineEdit* edit = project.find_draw_order_timeline_edit(*anim_name);
        if (edit == nullptr) {
            std::optional<DrawOrderTimelineEdit> copied =
                draw_order_edit_from_runtime(skeleton, *anim_name);
            if (!copied.has_value()) {
                restore_history_snapshot(state, before);
                return make_error("Could not resolve animation draw-order timeline.", op, spec);
            }
            project.draw_order_timeline_edits.push_back(std::move(*copied));
            edit = &project.draw_order_timeline_edits.back();
        }

        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const DrawOrderKeyframeEdit& keyframe, double key_time) {
                return keyframe.time < key_time;
            });
        if (key_it != edit->keyframes.end() && std::abs(key_it->time - *time) < kKeyTimeEpsilon) {
            key_it->slot_names = std::move(slot_order);
        } else {
            DrawOrderKeyframeEdit keyframe;
            keyframe.time = *time;
            keyframe.slot_names = std::move(slot_order);
            edit->keyframes.insert(key_it, std::move(keyframe));
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to apply draw-order keyframe: " + state->error_message, op, spec);
        }

        if (!record_action_from_snapshots(
                state,
                before,
                EditActionKind::AddKeyframe,
                "Set draw order keyframe via Agent",
                "Agent",
                bool_arg(args, "merge"))) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        return make_success(
            "Set draw-order keyframe successfully.",
            op,
            spec,
            object_value(std::move(preview)));
    }

    if (op == "remove_draw_order_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("remove_draw_order_keyframe requires 'args' object.", op, spec);
        }

        const auto anim_name = string_arg(*args, "animation");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !time.has_value()) {
            return make_error(
                "remove_draw_order_keyframe requires animation string and time number.",
                op,
                spec);
        }

        const EditorHistorySnapshot before = capture_history_snapshot(*state, true);
        auto edit_it = std::find_if(
            project.draw_order_timeline_edits.begin(),
            project.draw_order_timeline_edits.end(),
            [&](const DrawOrderTimelineEdit& edit) {
                return edit.animation_name == *anim_name;
            });
        if (edit_it == project.draw_order_timeline_edits.end()) {
            return make_error("Draw-order timeline edit not found.", op, spec, "not_found");
        }

        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const DrawOrderKeyframeEdit& keyframe) {
                return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            return make_error("Draw-order keyframe not found at that time.", op, spec, "not_found");
        }

        edit_it->keyframes.erase(key_it);
        if (edit_it->keyframes.empty()) {
            project.draw_order_timeline_edits.erase(edit_it);
        }

        if (!rebuild_project_runtime(state)) {
            restore_history_snapshot(state, before);
            return make_error("Failed to remove draw-order keyframe: " + state->error_message, op, spec);
        }

        if (!record_action_from_snapshots(
                state,
                before,
                EditActionKind::RemoveKeyframe,
                "Remove draw order keyframe via Agent",
                "Agent",
                false)) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        return make_success("Removed draw-order keyframe successfully.", op, spec);
    }

    return make_error("Unknown operation: " + std::string(op), op, spec, "unknown_operation");
}

} // namespace

AgentDispatchResult AgentCommandDispatcher::dispatch(ShellState* state, const json::Value& cmd) {
    AgentDispatchResult result = dispatch_impl(state, cmd);
    append_activity(state, &result);
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

} // namespace marrow::editor::shell
