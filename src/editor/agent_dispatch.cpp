#include "marrow/editor/agent_dispatch.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iterator>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

#include "shell_types.hpp"
#include "marrow/editor/project.hpp"

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
    {"undo", "edit", true, false},
    {"redo", "edit", true, false},
    {"set_transform", "edit", true, false},
    {"remove_transform_keyframe", "edit", true, false},
    {"edit_ik_constraint", "edit", true, false},
    {"set_draw_order_keyframe", "edit", true, false},
    {"remove_draw_order_keyframe", "edit", true, false},
    {"save", "management", true, true},
    {"export_runtime", "management", true, true},
};

const OperationSpec* find_operation(std::string_view op) {
    for (const OperationSpec& spec : kOperationSpecs) {
        if (spec.name == op) {
            return &spec;
        }
    }
    return nullptr;
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

std::optional<double> number_arg(const json::Value& args, std::string_view name) {
    const json::Value* value = json::find_member(args, name);
    if (value == nullptr || !value->is_number()) {
        return std::nullopt;
    }
    return value->as_number();
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
            request.kind == AgentReviewKind::SaveProject ? "save" : "export_runtime"));
    review.emplace("label", string_value(request.label));
    review.emplace("target_path", string_value(request.target_path.string()));
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
    bool binary_output) {
    AgentReviewRequest request;
    request.id = state->next_agent_review_id++;
    request.kind = kind;
    request.label = std::move(label);
    request.target_path = absolute_normalized(target_path);
    request.binary_output = binary_output;
    request.allowed = agent_path_allowed(*state, request.target_path);
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
