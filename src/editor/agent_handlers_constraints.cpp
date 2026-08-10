#include "agent_dispatch_internal.hpp"
#include "shell_constraints.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace marrow::editor::agent_detail {

namespace {

const json::Value* find_member_any(
    const json::Value& args,
    std::string_view primary,
    std::string_view fallback = {}) {
    if (const json::Value* value = json::find_member(args, primary)) {
        return value;
    }
    return fallback.empty() ? nullptr : json::find_member(args, fallback);
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

std::string path_spacing_mode_name(
    marrow::runtime::PathConstraintSpacingMode mode) {
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
    return apply_number_if_present(
               *value, "rotation", {}, &offsets->rotation, error_out) &&
        apply_number_if_present(*value, "x", {}, &offsets->x, error_out) &&
        apply_number_if_present(*value, "y", {}, &offsets->y, error_out) &&
        apply_number_if_present(
            *value, "scale_x", "scaleX", &offsets->scale_x, error_out) &&
        apply_number_if_present(
            *value, "scale_y", "scaleY", &offsets->scale_y, error_out) &&
        apply_number_if_present(
            *value, "shear_x", "shearX", &offsets->shear_x, error_out) &&
        apply_number_if_present(
            *value, "shear_y", "shearY", &offsets->shear_y, error_out);
}

json::Value xy_value(const marrow::runtime::AttachmentVertex& value) {
    json::Value::Object object;
    object.emplace("x", number_value(value.x));
    object.emplace("y", number_value(value.y));
    return object_value(std::move(object));
}

json::Value transform_offset_value(
    const marrow::runtime::TransformConstraintOffsets& offsets) {
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

struct PathConstraintTraits {
    using Edit = PathConstraintEdit;
    using RuntimeConstraint = marrow::runtime::PathConstraintData;

    static constexpr std::string_view args_error() {
        return "edit_path_constraint requires 'args' object.";
    }
    static constexpr std::string_view name_error() {
        return "edit_path_constraint requires 'name' string.";
    }
    static constexpr std::string_view not_found_error() {
        return "Path constraint not found.";
    }
    static constexpr std::string_view transaction_label() {
        return "Edit path constraint via Agent";
    }
    static constexpr std::string_view validated_message() {
        return "Path constraint edit validated.";
    }
    static constexpr std::string_view commit_failure_prefix() {
        return "Failed to apply path constraint edit: ";
    }
    static constexpr std::string_view success_message() {
        return "Edited path constraint successfully.";
    }

    static const Edit* find_project(
        const ProjectData& project,
        std::string_view name) {
        return project.find_path_constraint_edit(name);
    }
    static Edit* find_project(ProjectData& project, std::string_view name) {
        return project.find_path_constraint_edit(name);
    }
    static const RuntimeConstraint* find_runtime(
        const marrow::runtime::SkeletonData& skeleton,
        std::string_view name) {
        return shell::find_named_constraint(skeleton.path_constraints(), name);
    }
    static Edit materialize(
        const marrow::runtime::SkeletonData& skeleton,
        const RuntimeConstraint& constraint) {
        Edit edit;
        edit.name = constraint.name;
        if (constraint.slot_index < skeleton.slots().size()) {
            edit.slot_name = skeleton.slots()[constraint.slot_index].name;
        }
        edit.bone_names =
            names_from_indices(skeleton.bones(), constraint.bone_indices);
        edit.position = constraint.position;
        edit.spacing = constraint.spacing;
        edit.spacing_mode = constraint.spacing_mode;
        edit.rotate_mix = constraint.rotate_mix;
        edit.translate_mix = constraint.translate_mix;
        return edit;
    }
    static bool merge(
        const json::Value& args,
        const marrow::runtime::SkeletonData& skeleton,
        Edit* edit,
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
        if (!read_string_array_if_present(
                args, "bone_names", "bones", &edit->bone_names, error_out) ||
            !validate_bone_names(
                skeleton, edit->bone_names, "bone_names", error_out)) {
            return false;
        }

        if (const auto* spacing_mode_value =
                find_member_any(args, "spacing_mode", "spacingMode")) {
            if (!spacing_mode_value->is_string()) {
                *error_out = "spacing_mode must be 'length' or 'percent'.";
                return false;
            }
            if (spacing_mode_value->as_string() == "length") {
                edit->spacing_mode =
                    marrow::runtime::PathConstraintSpacingMode::Length;
            } else if (spacing_mode_value->as_string() == "percent") {
                edit->spacing_mode =
                    marrow::runtime::PathConstraintSpacingMode::Percent;
            } else {
                *error_out = "spacing_mode must be 'length' or 'percent'.";
                return false;
            }
        }

        if (!apply_number_if_present(
                args, "position", {}, &edit->position, error_out) ||
            !apply_number_if_present(
                args, "spacing", {}, &edit->spacing, error_out) ||
            !apply_number_if_present(
                args, "rotate_mix", "rotateMix", &edit->rotate_mix, error_out) ||
            !apply_number_if_present(
                args,
                "translate_mix",
                "translateMix",
                &edit->translate_mix,
                error_out)) {
            return false;
        }
        if (edit->position < 0.0 || edit->position > 1.0 ||
            edit->spacing < 0.0 || edit->rotate_mix < 0.0 ||
            edit->rotate_mix > 1.0 || edit->translate_mix < 0.0 ||
            edit->translate_mix > 1.0) {
            *error_out = "path constraint values are outside their valid ranges.";
            return false;
        }
        return true;
    }
    static void upsert(ProjectData& project, Edit edit) {
        if (Edit* existing = find_project(project, edit.name)) {
            *existing = std::move(edit);
        } else {
            project.path_constraint_edits.push_back(std::move(edit));
        }
    }
    static json::Value preview(const Edit& edit, bool dry_run) {
        json::Value::Object object;
        object.emplace("dry_run", bool_value(dry_run));
        object.emplace("name", string_value(edit.name));
        object.emplace("slot", string_value(edit.slot_name));
        object.emplace("bones", string_array_value(edit.bone_names));
        object.emplace("position", number_value(edit.position));
        object.emplace("spacing", number_value(edit.spacing));
        object.emplace(
            "spacing_mode", string_value(path_spacing_mode_name(edit.spacing_mode)));
        object.emplace("rotate_mix", number_value(edit.rotate_mix));
        object.emplace("translate_mix", number_value(edit.translate_mix));
        return object_value(std::move(object));
    }
};

struct TransformConstraintTraits {
    using Edit = TransformConstraintEdit;
    using RuntimeConstraint = marrow::runtime::TransformConstraintData;

    static constexpr std::string_view args_error() {
        return "edit_transform_constraint requires 'args' object.";
    }
    static constexpr std::string_view name_error() {
        return "edit_transform_constraint requires 'name' string.";
    }
    static constexpr std::string_view not_found_error() {
        return "Transform constraint not found.";
    }
    static constexpr std::string_view transaction_label() {
        return "Edit transform constraint via Agent";
    }
    static constexpr std::string_view validated_message() {
        return "Transform constraint edit validated.";
    }
    static constexpr std::string_view commit_failure_prefix() {
        return "Failed to apply transform constraint edit: ";
    }
    static constexpr std::string_view success_message() {
        return "Edited transform constraint successfully.";
    }

    static const Edit* find_project(
        const ProjectData& project,
        std::string_view name) {
        return project.find_transform_constraint_edit(name);
    }
    static Edit* find_project(ProjectData& project, std::string_view name) {
        return project.find_transform_constraint_edit(name);
    }
    static const RuntimeConstraint* find_runtime(
        const marrow::runtime::SkeletonData& skeleton,
        std::string_view name) {
        return shell::find_named_constraint(skeleton.transform_constraints(), name);
    }
    static Edit materialize(
        const marrow::runtime::SkeletonData& skeleton,
        const RuntimeConstraint& constraint) {
        Edit edit;
        edit.name = constraint.name;
        if (constraint.source_bone_index < skeleton.bones().size()) {
            edit.source_bone_name =
                skeleton.bones()[constraint.source_bone_index].name;
        }
        edit.bone_names = names_from_indices(
            skeleton.bones(), constraint.target_bone_indices);
        edit.rotate_mix = constraint.rotate_mix;
        edit.translate_mix = constraint.translate_mix;
        edit.scale_mix = constraint.scale_mix;
        edit.shear_mix = constraint.shear_mix;
        edit.offsets = constraint.offsets;
        return edit;
    }
    static bool merge(
        const json::Value& args,
        const marrow::runtime::SkeletonData& skeleton,
        Edit* edit,
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
        if (!read_string_array_if_present(
                args, "bone_names", "bones", &edit->bone_names, error_out) ||
            !validate_bone_names(
                skeleton, edit->bone_names, "bone_names", error_out)) {
            return false;
        }
        if (std::find(
                edit->bone_names.begin(),
                edit->bone_names.end(),
                edit->source_bone_name) != edit->bone_names.end()) {
            *error_out =
                "transform constraint source bone must not also be a target.";
            return false;
        }
        if (!apply_number_if_present(
                args, "rotate_mix", "rotateMix", &edit->rotate_mix, error_out) ||
            !apply_number_if_present(
                args,
                "translate_mix",
                "translateMix",
                &edit->translate_mix,
                error_out) ||
            !apply_number_if_present(
                args, "scale_mix", "scaleMix", &edit->scale_mix, error_out) ||
            !apply_number_if_present(
                args, "shear_mix", "shearMix", &edit->shear_mix, error_out) ||
            !apply_transform_offsets_if_present(args, &edit->offsets, error_out)) {
            return false;
        }
        if (edit->rotate_mix < 0.0 || edit->rotate_mix > 1.0 ||
            edit->translate_mix < 0.0 || edit->translate_mix > 1.0 ||
            edit->scale_mix < 0.0 || edit->scale_mix > 1.0 ||
            edit->shear_mix < 0.0 || edit->shear_mix > 1.0) {
            *error_out =
                "transform constraint mix values must stay within [0, 1].";
            return false;
        }
        return true;
    }
    static void upsert(ProjectData& project, Edit edit) {
        if (Edit* existing = find_project(project, edit.name)) {
            *existing = std::move(edit);
        } else {
            project.transform_constraint_edits.push_back(std::move(edit));
        }
    }
    static json::Value preview(const Edit& edit, bool dry_run) {
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
};

struct PhysicsConstraintTraits {
    using Edit = PhysicsConstraintEdit;
    using RuntimeConstraint = marrow::runtime::PhysicsConstraintData;

    static constexpr std::string_view args_error() {
        return "edit_physics_constraint requires 'args' object.";
    }
    static constexpr std::string_view name_error() {
        return "edit_physics_constraint requires 'name' string.";
    }
    static constexpr std::string_view not_found_error() {
        return "Physics constraint not found.";
    }
    static constexpr std::string_view transaction_label() {
        return "Edit physics constraint via Agent";
    }
    static constexpr std::string_view validated_message() {
        return "Physics constraint edit validated.";
    }
    static constexpr std::string_view commit_failure_prefix() {
        return "Failed to apply physics constraint edit: ";
    }
    static constexpr std::string_view success_message() {
        return "Edited physics constraint successfully.";
    }

    static const Edit* find_project(
        const ProjectData& project,
        std::string_view name) {
        return project.find_physics_constraint_edit(name);
    }
    static Edit* find_project(ProjectData& project, std::string_view name) {
        return project.find_physics_constraint_edit(name);
    }
    static const RuntimeConstraint* find_runtime(
        const marrow::runtime::SkeletonData& skeleton,
        std::string_view name) {
        return shell::find_named_constraint(skeleton.physics_constraints(), name);
    }
    static Edit materialize(
        const marrow::runtime::SkeletonData& skeleton,
        const RuntimeConstraint& constraint) {
        Edit edit;
        edit.name = constraint.name;
        edit.bone_names =
            names_from_indices(skeleton.bones(), constraint.bone_indices);
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
    static bool merge(
        const json::Value& args,
        const marrow::runtime::SkeletonData& skeleton,
        Edit* edit,
        std::string* error_out) {
        if (!read_string_array_if_present(
                args, "bone_names", "bones", &edit->bone_names, error_out) ||
            !validate_bone_names(
                skeleton, edit->bone_names, "bone_names", error_out)) {
            return false;
        }
        if (!apply_number_if_present(args, "step", {}, &edit->step, error_out) ||
            !apply_number_if_present(args, "x", {}, &edit->x, error_out) ||
            !apply_number_if_present(args, "y", {}, &edit->y, error_out) ||
            !apply_number_if_present(args, "rotate", {}, &edit->rotate, error_out) ||
            !apply_number_if_present(
                args, "scale_x", "scaleX", &edit->scale_x, error_out) ||
            !apply_number_if_present(
                args, "shear_x", "shearX", &edit->shear_x, error_out) ||
            !apply_number_if_present(args, "limit", {}, &edit->limit, error_out) ||
            !apply_number_if_present(
                args, "inertia", {}, &edit->inertia, error_out) ||
            !apply_number_if_present(
                args, "damping", {}, &edit->damping, error_out) ||
            !apply_number_if_present(
                args, "strength", {}, &edit->strength, error_out) ||
            !apply_number_if_present(
                args,
                "mass_inverse",
                "massInverse",
                &edit->mass_inverse,
                error_out) ||
            !apply_number_if_present(args, "mix", {}, &edit->mix, error_out) ||
            !apply_xy_if_present(args, "gravity", &edit->gravity, error_out) ||
            !apply_xy_if_present(args, "wind", &edit->wind, error_out)) {
            return false;
        }
        if (edit->step <= 0.0 || edit->x < 0.0 || edit->y < 0.0 ||
            edit->rotate < 0.0 || edit->scale_x < 0.0 ||
            edit->shear_x < 0.0 || edit->limit < 0.0 || edit->inertia < 0.0 ||
            edit->inertia > 1.0 || edit->damping < 0.0 ||
            edit->strength < 0.0 || edit->mass_inverse < 0.0 ||
            edit->mix < 0.0 || edit->mix > 1.0) {
            *error_out = "physics constraint values are outside their valid ranges.";
            return false;
        }
        return true;
    }
    static void upsert(ProjectData& project, Edit edit) {
        if (Edit* existing = find_project(project, edit.name)) {
            *existing = std::move(edit);
        } else {
            project.physics_constraint_edits.push_back(std::move(edit));
        }
    }
    static json::Value preview(const Edit& edit, bool dry_run) {
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
};

template <typename Traits>
AgentDispatchResult handle_constraint_edit(
    EditorSession& session,
    const json::Value& cmd,
    std::string_view op,
    const OperationSpec* spec) {
    const json::Value* args = command_args(cmd);
    if (args == nullptr) {
        return make_error(std::string(Traits::args_error()), op, spec);
    }
    const auto name = string_arg(*args, "name");
    if (!name.has_value()) {
        return make_error(std::string(Traits::name_error()), op, spec);
    }

    const ProjectData& project = *session.project();
    const marrow::runtime::SkeletonData& skeleton = *session.runtime_data();
    typename Traits::Edit merged;
    if (const typename Traits::Edit* existing =
            Traits::find_project(project, *name)) {
        merged = *existing;
    } else if (const typename Traits::RuntimeConstraint* runtime_constraint =
                   Traits::find_runtime(skeleton, *name)) {
        merged = Traits::materialize(skeleton, *runtime_constraint);
    } else {
        return make_error(
            std::string(Traits::not_found_error()), op, spec, "not_found");
    }

    std::string merge_error;
    if (!Traits::merge(*args, skeleton, &merged, &merge_error)) {
        return make_error(std::move(merge_error), op, spec);
    }
    if (bool_arg(args, "dry_run")) {
        return make_success(
            std::string(Traits::validated_message()),
            op,
            spec,
            Traits::preview(merged, true));
    }

    auto transaction = session.begin_edit({
        EditKind::EditProperty,
        std::string(Traits::transaction_label()),
        "Agent",
        bool_arg(args, "merge"),
        EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
    if (!transaction) {
        return make_error(
            transaction.error()->format(), op, spec, "transaction_active");
    }

    json::Value live_delta = Traits::preview(merged, false);
    Traits::upsert(*transaction.project(), std::move(merged));
    if (auto result = commit_or_error(
            transaction,
            op,
            spec,
            CommitPolicy{Traits::commit_failure_prefix()})) {
        return std::move(*result);
    }
    return make_success(
        std::string(Traits::success_message()),
        op,
        spec,
        std::move(live_delta));
}

} // namespace

AgentDispatchResult handle_constraint_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation) {
    EditorSession& session = context.session;
    const std::string_view op = operation.name;
    const OperationSpec* spec = &operation;
    const ProjectData& project = *session.project();
    const marrow::runtime::SkeletonData& skeleton = *session.runtime_data();

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
            const marrow::runtime::IkConstraintData* runtime_constraint =
                shell::find_named_constraint(skeleton.ik_constraints(), *name);
            if (project_edit == nullptr && runtime_constraint == nullptr) {
                return make_error(
                    "IK constraint not found in runtime skeleton.",
                    op,
                    spec,
                    "not_found");
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
            return make_success(
                "IK constraint edit validated.",
                op,
                spec,
                object_value(std::move(preview)));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Edit IK Constraint via Agent",
            "Agent",
            bool_arg(args, "merge"),
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& editable_project = *transaction.project();
        IkConstraintEdit* edit = editable_project.find_ik_constraint_edit(*name);
        if (edit == nullptr) {
            const marrow::runtime::IkConstraintData* runtime_constraint =
                shell::find_named_constraint(skeleton.ik_constraints(), *name);
            if (runtime_constraint == nullptr) {
                return make_error(
                    "IK constraint not found in runtime skeleton.",
                    op,
                    spec,
                    "not_found");
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

            editable_project.ik_constraint_edits.push_back(std::move(new_edit));
            edit = &editable_project.ik_constraint_edits.back();
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

        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to apply IK constraint edit: "})) {
            return std::move(*result);
        }

        return make_success("Edited IK constraint successfully.", op, spec);
    }

    if (op == "edit_path_constraint") {
        return handle_constraint_edit<PathConstraintTraits>(session, cmd, op, spec);
    }
    if (op == "edit_transform_constraint") {
        return handle_constraint_edit<TransformConstraintTraits>(session, cmd, op, spec);
    }
    if (op == "edit_physics_constraint") {
        return handle_constraint_edit<PhysicsConstraintTraits>(session, cmd, op, spec);
    }

    return make_error(
        "Unknown operation: " + std::string(op),
        op,
        spec,
        "unknown_operation");
}

} // namespace marrow::editor::agent_detail
