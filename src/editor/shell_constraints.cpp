#include "shell_constraints.hpp"

#include "shell_preview.hpp"

#include <algorithm>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_state.hpp"
#include "shell_widgets.hpp"
#include "marrow/editor/agent_dispatch.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;
using marrow::editor::IconRegistry;

namespace {

template <typename MutateFn>
bool apply_constraint_project_drag(
    ShellState* state,
    bool changed,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    std::string failure_status,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const ImGuiID item_id = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) {
        state->pending_edit_action = PendingEditAction{
            item_id,
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            capture_history_snapshot(*state)};
    }

    if (changed) {
        const EditorHistorySnapshot rollback = capture_history_snapshot(*state, false);
        mutate();
        if (!rebuild_project_runtime(state)) {
            const std::string rebuild_error = state->error_message;
            restore_history_snapshot(state, rollback);
            state->pending_edit_action.reset();
            state->error_message = rebuild_error;
            state->status_message = std::move(failure_status);
            return false;
        }
    }

    if (ImGui::IsItemDeactivatedAfterEdit() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        PendingEditAction pending = std::move(*state->pending_edit_action);
        state->pending_edit_action.reset();
        return record_action_from_snapshots(
            state,
            pending.before_snapshot,
            pending.kind,
            std::move(pending.label),
            std::move(pending.group),
            pending.allow_merge);
    }

    if (ImGui::IsItemDeactivated() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        state->pending_edit_action.reset();
    }

    return true;
}

} // namespace

const char* constraint_kind_label(ConstraintKind kind) {
    switch (kind) {
    case ConstraintKind::Ik:
        return "IK";
    case ConstraintKind::Path:
        return "Path";
    case ConstraintKind::Transform:
        return "Transform";
    case ConstraintKind::Physics:
        return "Physics";
    }

    return "Constraint";
}

std::optional<std::string> named_bone_if_exists(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view bone_name) {
    return skeleton.find_bone_index(bone_name).has_value()
        ? std::optional<std::string>(std::string(bone_name))
        : std::nullopt;
}

std::optional<std::string> named_slot_if_exists(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view slot_name) {
    return skeleton.find_slot_index(slot_name).has_value()
        ? std::optional<std::string>(std::string(slot_name))
        : std::nullopt;
}

std::vector<std::string> all_bone_names(const marrow::runtime::SkeletonData& skeleton) {
    std::vector<std::string> names;
    names.reserve(skeleton.bones().size());
    for (const auto& bone : skeleton.bones()) {
        names.push_back(bone.name);
    }
    return names;
}

std::vector<std::string> path_slot_names(const marrow::runtime::SkeletonData& skeleton) {
    std::vector<std::string> names;
    names.reserve(skeleton.slots().size());
    for (std::size_t slot_index = 0; slot_index < skeleton.slots().size(); ++slot_index) {
        const auto& slot = skeleton.slots()[slot_index];
        const auto* attachment =
            skeleton.find_attachment_source(slot_index, slot.setup_attachment);
        if (attachment != nullptr && attachment->path_attachment.has_value()) {
            names.push_back(slot.name);
        }
    }
    return names;
}

std::optional<std::string> first_non_root_bone_name(
    const marrow::runtime::SkeletonData& skeleton) {
    for (const auto& bone : skeleton.bones()) {
        if (bone.parent_index.has_value()) {
            return bone.name;
        }
    }
    if (!skeleton.bones().empty()) {
        return skeleton.bones().front().name;
    }
    return std::nullopt;
}

std::optional<std::string> first_constraint_target_name(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& excluded_bones) {
    for (const auto& bone : skeleton.bones()) {
        if (std::find(excluded_bones.begin(), excluded_bones.end(), bone.name) ==
            excluded_bones.end()) {
            return bone.name;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> first_child_bone_index(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t parent_index) {
    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        if (skeleton.bones()[bone_index].parent_index ==
            std::optional<std::size_t>{parent_index}) {
            return bone_index;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> preferred_chain(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string_view>& preferred_names) {
    std::vector<std::string> names;
    names.reserve(preferred_names.size());
    for (const std::string_view name : preferred_names) {
        if (!skeleton.find_bone_index(name).has_value()) {
            return std::nullopt;
        }
        names.emplace_back(name);
    }
    return names;
}

std::optional<std::vector<std::string>> first_direct_chain(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t length) {
    if (length == 0U || skeleton.bones().empty()) {
        return std::nullopt;
    }

    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        std::vector<std::string> chain;
        chain.push_back(skeleton.bones()[bone_index].name);
        std::size_t current_index = bone_index;
        while (chain.size() < length) {
            const auto child_index = first_child_bone_index(skeleton, current_index);
            if (!child_index.has_value()) {
                break;
            }
            chain.push_back(skeleton.bones()[*child_index].name);
            current_index = *child_index;
        }
        if (chain.size() == length) {
            return chain;
        }
    }

    return std::nullopt;
}

bool constraint_exists(
    const marrow::runtime::SkeletonData& skeleton,
    ConstraintKind kind,
    std::string_view name) {
    switch (kind) {
    case ConstraintKind::Ik:
        return find_named_constraint(skeleton.ik_constraints(), name) != nullptr;
    case ConstraintKind::Path:
        return find_named_constraint(skeleton.path_constraints(), name) != nullptr;
    case ConstraintKind::Transform:
        return find_named_constraint(skeleton.transform_constraints(), name) != nullptr;
    case ConstraintKind::Physics:
        return find_named_constraint(skeleton.physics_constraints(), name) != nullptr;
    }

    return false;
}

void validate_selected_constraint(ShellState* state) {
    const auto selection = selected_constraint(*state);
    if (!state->load_result || !selection.has_value()) {
        return;
    }
    if (!constraint_exists(
            *state->load_result.skeleton_data,
            selection->kind,
            selection->constraint_name)) {
        state->selection.remap(*selection, std::nullopt);
    }
}

void select_constraint(
    ShellState* state,
    ConstraintKind kind,
    std::string_view name,
    std::string_view source,
    bool update_status_message) {
    state->selection.replace(
        marrow::editor::ConstraintSelection{kind, std::string(name)});
    if (!update_status_message) {
        return;
    }

    std::ostringstream stream;
    stream << "Selected " << constraint_kind_label(kind) << " constraint " << name;
    if (!source.empty()) {
        stream << " via " << source;
    }
    state->status_message = stream.str();
}

std::string unique_constraint_name(
    const ShellState& state,
    ConstraintKind kind,
    std::string_view prefix) {
    if (!state.load_result) {
        return std::string(prefix) + "_1";
    }

    for (int index = 1; index < 1000; ++index) {
        const std::string candidate = std::string(prefix) + "_" + std::to_string(index);
        if (!constraint_exists(*state.load_result.skeleton_data, kind, candidate)) {
            return candidate;
        }
    }

    return std::string(prefix) + "_overflow";
}

std::optional<marrow::editor::IkConstraintEdit> make_ik_constraint_edit_from_runtime(
    const ShellState& state,
    std::string_view name) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto* constraint =
        find_named_constraint(state.load_result.skeleton_data->ik_constraints(), name);
    if (constraint == nullptr) {
        return std::nullopt;
    }

    marrow::editor::IkConstraintEdit edit;
    edit.name = constraint->name;
    for (const std::size_t bone_index : constraint->bone_indices) {
        if (bone_index >= state.load_result.skeleton_data->bones().size()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(state.load_result.skeleton_data->bones()[bone_index].name);
    }
    if (constraint->target_bone_index >= state.load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }
    edit.target_bone_name =
        state.load_result.skeleton_data->bones()[constraint->target_bone_index].name;
    edit.mix = constraint->mix;
    edit.bend_positive = constraint->bend_positive;
    edit.softness = constraint->softness;
    edit.compress = constraint->compress;
    edit.stretch = constraint->stretch;
    return edit;
}

std::optional<marrow::editor::PathConstraintEdit> make_path_constraint_edit_from_runtime(
    const ShellState& state,
    std::string_view name) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto* constraint =
        find_named_constraint(state.load_result.skeleton_data->path_constraints(), name);
    if (constraint == nullptr) {
        return std::nullopt;
    }

    marrow::editor::PathConstraintEdit edit;
    edit.name = constraint->name;
    if (constraint->slot_index >= state.load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }
    edit.slot_name = state.load_result.skeleton_data->slots()[constraint->slot_index].name;
    for (const std::size_t bone_index : constraint->bone_indices) {
        if (bone_index >= state.load_result.skeleton_data->bones().size()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(state.load_result.skeleton_data->bones()[bone_index].name);
    }
    edit.position = constraint->position;
    edit.spacing = constraint->spacing;
    edit.spacing_mode = constraint->spacing_mode;
    edit.rotate_mix = constraint->rotate_mix;
    edit.translate_mix = constraint->translate_mix;
    return edit;
}

std::optional<marrow::editor::TransformConstraintEdit> make_transform_constraint_edit_from_runtime(
    const ShellState& state,
    std::string_view name) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto* constraint =
        find_named_constraint(state.load_result.skeleton_data->transform_constraints(), name);
    if (constraint == nullptr) {
        return std::nullopt;
    }

    marrow::editor::TransformConstraintEdit edit;
    edit.name = constraint->name;
    if (constraint->source_bone_index >= state.load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }
    edit.source_bone_name =
        state.load_result.skeleton_data->bones()[constraint->source_bone_index].name;
    for (const std::size_t bone_index : constraint->target_bone_indices) {
        if (bone_index >= state.load_result.skeleton_data->bones().size()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(state.load_result.skeleton_data->bones()[bone_index].name);
    }
    edit.rotate_mix = constraint->rotate_mix;
    edit.translate_mix = constraint->translate_mix;
    edit.scale_mix = constraint->scale_mix;
    edit.shear_mix = constraint->shear_mix;
    edit.offsets = constraint->offsets;
    return edit;
}

std::optional<marrow::editor::PhysicsConstraintEdit> make_physics_constraint_edit_from_runtime(
    const ShellState& state,
    std::string_view name) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto* constraint =
        find_named_constraint(state.load_result.skeleton_data->physics_constraints(), name);
    if (constraint == nullptr) {
        return std::nullopt;
    }

    marrow::editor::PhysicsConstraintEdit edit;
    edit.name = constraint->name;
    for (const std::size_t bone_index : constraint->bone_indices) {
        if (bone_index >= state.load_result.skeleton_data->bones().size()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(state.load_result.skeleton_data->bones()[bone_index].name);
    }
    edit.step = constraint->step;
    edit.x = constraint->x;
    edit.y = constraint->y;
    edit.rotate = constraint->rotate;
    edit.scale_x = constraint->scale_x;
    edit.shear_x = constraint->shear_x;
    edit.limit = constraint->limit;
    edit.inertia = constraint->inertia;
    edit.damping = constraint->damping;
    edit.strength = constraint->strength;
    edit.mass_inverse = constraint->mass_inverse;
    edit.gravity = constraint->gravity;
    edit.wind = constraint->wind;
    edit.mix = constraint->mix;
    return edit;
}

std::optional<std::size_t> ensure_ik_constraint_edit_index(
    ShellState* state,
    std::string_view name) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return std::nullopt;
    }
    const auto existing = std::find_if(
        state->load_result.project->ik_constraint_edits.begin(),
        state->load_result.project->ik_constraint_edits.end(),
        [&](const marrow::editor::IkConstraintEdit& edit) {
            return edit.name == name;
        });
    if (existing != state->load_result.project->ik_constraint_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(state->load_result.project->ik_constraint_edits.begin(), existing));
    }

    const auto edit = make_ik_constraint_edit_from_runtime(*state, name);
    if (!edit.has_value()) {
        return std::nullopt;
    }
    state->load_result.project->ik_constraint_edits.push_back(*edit);
    return state->load_result.project->ik_constraint_edits.size() - 1U;
}

std::optional<std::size_t> ensure_path_constraint_edit_index(
    ShellState* state,
    std::string_view name) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return std::nullopt;
    }
    const auto existing = std::find_if(
        state->load_result.project->path_constraint_edits.begin(),
        state->load_result.project->path_constraint_edits.end(),
        [&](const marrow::editor::PathConstraintEdit& edit) {
            return edit.name == name;
        });
    if (existing != state->load_result.project->path_constraint_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(state->load_result.project->path_constraint_edits.begin(), existing));
    }

    const auto edit = make_path_constraint_edit_from_runtime(*state, name);
    if (!edit.has_value()) {
        return std::nullopt;
    }
    state->load_result.project->path_constraint_edits.push_back(*edit);
    return state->load_result.project->path_constraint_edits.size() - 1U;
}

std::optional<std::size_t> ensure_transform_constraint_edit_index(
    ShellState* state,
    std::string_view name) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return std::nullopt;
    }
    const auto existing = std::find_if(
        state->load_result.project->transform_constraint_edits.begin(),
        state->load_result.project->transform_constraint_edits.end(),
        [&](const marrow::editor::TransformConstraintEdit& edit) {
            return edit.name == name;
        });
    if (existing != state->load_result.project->transform_constraint_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(state->load_result.project->transform_constraint_edits.begin(), existing));
    }

    const auto edit = make_transform_constraint_edit_from_runtime(*state, name);
    if (!edit.has_value()) {
        return std::nullopt;
    }
    state->load_result.project->transform_constraint_edits.push_back(*edit);
    return state->load_result.project->transform_constraint_edits.size() - 1U;
}

std::optional<std::size_t> ensure_physics_constraint_edit_index(
    ShellState* state,
    std::string_view name) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return std::nullopt;
    }
    const auto existing = std::find_if(
        state->load_result.project->physics_constraint_edits.begin(),
        state->load_result.project->physics_constraint_edits.end(),
        [&](const marrow::editor::PhysicsConstraintEdit& edit) {
            return edit.name == name;
        });
    if (existing != state->load_result.project->physics_constraint_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(state->load_result.project->physics_constraint_edits.begin(), existing));
    }

    const auto edit = make_physics_constraint_edit_from_runtime(*state, name);
    if (!edit.has_value()) {
        return std::nullopt;
    }
    state->load_result.project->physics_constraint_edits.push_back(*edit);
    return state->load_result.project->physics_constraint_edits.size() - 1U;
}

std::optional<marrow::editor::IkConstraintEdit> make_default_ik_constraint_edit(
    const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto& skeleton = *state.load_result.skeleton_data;

    marrow::editor::IkConstraintEdit edit;
    edit.name = unique_constraint_name(state, ConstraintKind::Ik, "ik_constraint");
    edit.bone_names = preferred_chain(skeleton, {"ik_upper", "ik_lower"})
        .value_or(first_direct_chain(skeleton, 2).value_or(std::vector<std::string>{}));
    if (edit.bone_names.empty()) {
        const auto single_bone = first_non_root_bone_name(skeleton);
        if (!single_bone.has_value()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(*single_bone);
    }
    edit.target_bone_name = named_bone_if_exists(skeleton, "ik_target")
        .value_or(first_constraint_target_name(skeleton, edit.bone_names).value_or(std::string{}));
    if (edit.target_bone_name.empty()) {
        return std::nullopt;
    }
    return edit;
}

std::optional<marrow::editor::PathConstraintEdit> make_default_path_constraint_edit(
    const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto& skeleton = *state.load_result.skeleton_data;

    marrow::editor::PathConstraintEdit edit;
    edit.name = unique_constraint_name(state, ConstraintKind::Path, "path_constraint");
    edit.slot_name = named_slot_if_exists(skeleton, "guide")
        .value_or(path_slot_names(skeleton).empty() ? std::string{} : path_slot_names(skeleton).front());
    if (edit.slot_name.empty()) {
        return std::nullopt;
    }
    edit.bone_names = preferred_chain(skeleton, {"path_a", "path_b", "path_c"})
        .value_or(first_direct_chain(skeleton, 3).value_or(std::vector<std::string>{}));
    if (edit.bone_names.empty()) {
        return std::nullopt;
    }
    edit.position = 0.1;
    edit.spacing = 0.3;
    edit.spacing_mode = marrow::runtime::PathConstraintSpacingMode::Percent;
    edit.rotate_mix = 1.0;
    edit.translate_mix = 1.0;
    return edit;
}

std::optional<marrow::editor::TransformConstraintEdit> make_default_transform_constraint_edit(
    const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto& skeleton = *state.load_result.skeleton_data;

    marrow::editor::TransformConstraintEdit edit;
    edit.name =
        unique_constraint_name(state, ConstraintKind::Transform, "transform_constraint");
    edit.source_bone_name = named_bone_if_exists(skeleton, "transform_source")
        .value_or(first_non_root_bone_name(skeleton).value_or(std::string{}));
    if (edit.source_bone_name.empty()) {
        return std::nullopt;
    }
    if (const auto preferred_target = named_bone_if_exists(skeleton, "transform_target")) {
        edit.bone_names = {*preferred_target};
    } else if (const auto target_name = first_constraint_target_name(
                   skeleton,
                   std::vector<std::string>{edit.source_bone_name})) {
        edit.bone_names = {*target_name};
    } else {
        return std::nullopt;
    }
    edit.rotate_mix = 0.5;
    edit.translate_mix = 0.25;
    edit.scale_mix = 1.0;
    edit.shear_mix = 0.75;
    edit.offsets.rotation = 15.0;
    edit.offsets.x = -10.0;
    edit.offsets.y = 20.0;
    edit.offsets.scale_x = 0.2;
    edit.offsets.scale_y = -0.1;
    edit.offsets.shear_x = 5.0;
    edit.offsets.shear_y = -2.0;
    return edit;
}

std::optional<marrow::editor::PhysicsConstraintEdit> make_default_physics_constraint_edit(
    const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto& skeleton = *state.load_result.skeleton_data;

    marrow::editor::PhysicsConstraintEdit edit;
    edit.name = unique_constraint_name(state, ConstraintKind::Physics, "physics_constraint");
    edit.bone_names = preferred_chain(skeleton, {"ribbon_01", "ribbon_02"})
        .value_or(first_direct_chain(skeleton, 2).value_or(std::vector<std::string>{}));
    if (edit.bone_names.empty()) {
        return std::nullopt;
    }
    edit.step = 1.0 / 60.0;
    edit.x = 1.0;
    edit.y = 1.0;
    edit.rotate = 1.0;
    edit.scale_x = 1.0;
    edit.shear_x = 0.0;
    edit.limit = 30.0;
    edit.inertia = 0.85;
    edit.damping = 4.0;
    edit.strength = 18.0;
    edit.mass_inverse = 1.0;
    edit.gravity = {0.0, -24.0};
    edit.wind = {12.0, 0.0};
    edit.mix = 1.0;
    return edit;
}

bool draw_string_combo(
    const char* label,
    const std::vector<std::string>& options,
    std::string* value) {
    const char* preview = value->empty() ? "<none>" : value->c_str();
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (const std::string& option : options) {
            const bool selected = *value == option;
            if (ImGui::Selectable(option.c_str(), selected)) {
                *value = option;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}


void draw_constraints_window(ShellState* state) {
    ImGui::Begin(kConstraintsWindowTitle);
    widgets::panel_head(state->icons, Icon::ConstraintIk, "Constraints");

    if (!state->load_result || state->load_result.project == nullptr) {
        ImGui::TextUnformatted("Load a valid project to author constraint overrides.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    auto* project = state->load_result.project.get();
    const std::vector<std::string> bone_options = all_bone_names(skeleton);
    const std::vector<std::string> path_slots = path_slot_names(skeleton);
    constexpr double kZero = 0.0;
    constexpr double kOne = 1.0;
    constexpr double kTen = 10.0;

    validate_selected_constraint(state);

    const auto constraint_group = [&](ConstraintKind kind, std::string_view name) {
        return std::string("constraint:") + constraint_kind_label(kind) + ":" +
            std::string(name);
    };
    const auto commit_constraint_change = [&](const marrow::editor::ProjectData& previous_project,
                                              ConstraintKind kind,
                                              std::string_view name,
                                              std::string failure_message,
                                              std::string success_message,
                                              bool allow_merge = true) {
        if (!apply_project_command_change(
                state,
                previous_project,
                EditActionKind::EditProperty,
                std::move(success_message),
                constraint_group(kind, name),
                allow_merge,
                std::move(failure_message))) {
            return false;
        }

        select_constraint(state, kind, name, "", false);
        return true;
    };

    const auto append_direct_child_name = [&](std::vector<std::string>* bone_names) {
        if (bone_names == nullptr || bone_names->empty()) {
            return false;
        }
        const auto last_bone_index = skeleton.find_bone_index(bone_names->back());
        if (!last_bone_index.has_value()) {
            return false;
        }
        const auto child_index = first_child_bone_index(skeleton, *last_bone_index);
        if (!child_index.has_value()) {
            return false;
        }
        const std::string& child_name = skeleton.bones()[*child_index].name;
        if (std::find(bone_names->begin(), bone_names->end(), child_name) != bone_names->end()) {
            return false;
        }
        bone_names->push_back(child_name);
        return true;
    };

    {
        // Type tabs as a segmented accent strip (v2 vocabulary), replacing
        // the default ImGui tab bar. Active type is persisted in ShellState.
        static const char* const kConstraintTabs[] = {
            "IK", "Path", "Transform", "Physics"};
        widgets::seg_toggle(
            "##constraint_tabs", kConstraintTabs, 4,
            &state->constraints_tab);
        ImGui::Spacing();
        if (state->constraints_tab == 0) {
            if (ImGui::Button("Add IK Constraint")) {
                const marrow::editor::ProjectData previous_project = *project;
                if (const auto new_edit = make_default_ik_constraint_edit(*state)) {
                    project->ik_constraint_edits.push_back(*new_edit);
                    commit_constraint_change(
                        previous_project,
                        ConstraintKind::Ik,
                        new_edit->name,
                        "IK constraint edit failed",
                        "Added IK constraint " + new_edit->name);
                } else {
                    state->status_message = "Could not derive a valid default IK constraint";
                }
            }

            ImGui::BeginChild("ik_constraint_list", ImVec2(0.0f, 120.0f), true);
            for (const auto& constraint : skeleton.ik_constraints()) {
                const bool selected =
                    selected_constraint(*state).has_value() &&
                    selected_constraint(*state)->kind == ConstraintKind::Ik &&
                    selected_constraint(*state)->constraint_name == constraint.name;
                const bool has_project_edit =
                    project->find_ik_constraint_edit(constraint.name) != nullptr;
                const std::string label =
                    constraint.name + (has_project_edit ? "  [project]" : "  [runtime]");
                if (icon_selectable(
                        state->icons,
                        Icon::ConstraintIk,
                        label.c_str(),
                        selected)) {
                    select_constraint(state, ConstraintKind::Ik, constraint.name, "Constraints", true);
                }
            }
            ImGui::EndChild();

            const std::string selected_name =
                selected_constraint(*state).has_value() &&
                    selected_constraint(*state)->kind == ConstraintKind::Ik &&
                    find_named_constraint(skeleton.ik_constraints(), selected_constraint(*state)->constraint_name) != nullptr
                ? selected_constraint(*state)->constraint_name
                : (!skeleton.ik_constraints().empty() ? skeleton.ik_constraints().front().name : std::string{});
            if (selected_name.empty()) {
                ImGui::TextUnformatted("No IK constraints are active in the current runtime preview.");
            } else {
                const auto runtime_edit = make_ik_constraint_edit_from_runtime(*state, selected_name);
                const marrow::editor::IkConstraintEdit* project_edit =
                    project->find_ik_constraint_edit(selected_name);
                const marrow::editor::IkConstraintEdit display_edit =
                    project_edit != nullptr ? *project_edit : *runtime_edit;

                ImGui::Separator();
                ImGui::Text("Name: %s", display_edit.name.c_str());
                ImGui::Text(
                    "Source: %s",
                    project_edit != nullptr ? "project constraint edit" : "runtime constraint");

                int chain_length = static_cast<int>(display_edit.bone_names.size());
                if (ImGui::RadioButton("1 Bone", chain_length == 1)) {
                    if (display_edit.bone_names.size() > 1U) {
                        namespace json = marrow::runtime::json;
                        json::Value::Object cmd_obj;
                        cmd_obj.emplace("op", json::Value("edit_ik_constraint", {}));
                        json::Value::Object args_obj;
                        args_obj.emplace("name", json::Value(selected_name, {}));
                        json::Value::Array bone_names_arr;
                        bone_names_arr.push_back(json::Value(display_edit.bone_names[0], {}));
                        args_obj.emplace("bone_names", json::Value(std::move(bone_names_arr), {}));
                        cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
                        dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
                    }
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("2 Bones", chain_length == 2)) {
                    if (display_edit.bone_names.size() < 2U) {
                        std::vector<std::string> new_bones = display_edit.bone_names;
                        if (append_direct_child_name(&new_bones)) {
                            namespace json = marrow::runtime::json;
                            json::Value::Object cmd_obj;
                            cmd_obj.emplace("op", json::Value("edit_ik_constraint", {}));
                            json::Value::Object args_obj;
                            args_obj.emplace("name", json::Value(selected_name, {}));
                            json::Value::Array bone_names_arr;
                            for (const auto& b : new_bones) bone_names_arr.push_back(json::Value(b, {}));
                            args_obj.emplace("bone_names", json::Value(std::move(bone_names_arr), {}));
                            cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
                            dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
                        }
                    }
                }

                for (std::size_t bone_index = 0; bone_index < display_edit.bone_names.size(); ++bone_index) {
                    std::string edited_bone = display_edit.bone_names[bone_index];
                    const std::string label = "Bone " + std::to_string(bone_index + 1U);
                    if (draw_string_combo(label.c_str(), bone_options, &edited_bone)) {
                        namespace json = marrow::runtime::json;
                        json::Value::Object cmd_obj;
                        cmd_obj.emplace("op", json::Value("edit_ik_constraint", {}));
                        json::Value::Object args_obj;
                        args_obj.emplace("name", json::Value(selected_name, {}));
                        json::Value::Array bone_names_arr;
                        for (std::size_t i = 0; i < display_edit.bone_names.size(); ++i) {
                            bone_names_arr.push_back(json::Value(i == bone_index ? edited_bone : display_edit.bone_names[i], {}));
                        }
                        args_obj.emplace("bone_names", json::Value(std::move(bone_names_arr), {}));
                        cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
                        dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
                    }
                }

                std::string edited_target = display_edit.target_bone_name;
                if (draw_string_combo("Target", bone_options, &edited_target)) {
                    namespace json = marrow::runtime::json;
                    json::Value::Object cmd_obj;
                    cmd_obj.emplace("op", json::Value("edit_ik_constraint", {}));
                    json::Value::Object args_obj;
                    args_obj.emplace("name", json::Value(selected_name, {}));
                    args_obj.emplace("target", json::Value(edited_target, {}));
                    cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
                    dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
                }

                double edited_mix = display_edit.mix;
                const bool mix_changed = ImGui::SliderScalar(
                    "Mix",
                    ImGuiDataType_Double,
                    &edited_mix,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_constraint_project_drag(
                    state,
                    mix_changed,
                    EditActionKind::EditProperty,
                    "Updated IK mix on " + selected_name,
                    constraint_group(ConstraintKind::Ik, selected_name),
                    false,
                    "IK constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_ik_constraint_edit_index(state, selected_name)) {
                            project->ik_constraint_edits[*edit_index].mix = edited_mix;
                        }
                    });

                bool bend_positive = display_edit.bend_positive;
                if (ImGui::Checkbox("Bend Positive", &bend_positive)) {
                    namespace json = marrow::runtime::json;
                    json::Value::Object cmd_obj;
                    cmd_obj.emplace("op", json::Value("edit_ik_constraint", {}));
                    json::Value::Object args_obj;
                    args_obj.emplace("name", json::Value(selected_name, {}));
                    args_obj.emplace("bend_positive", json::Value(bend_positive, {}));
                    cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
                    dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
                }
            }

        }

        if (state->constraints_tab == 1) {
            if (ImGui::Button("Add Path Constraint")) {
                const marrow::editor::ProjectData previous_project = *project;
                if (const auto new_edit = make_default_path_constraint_edit(*state)) {
                    project->path_constraint_edits.push_back(*new_edit);
                    commit_constraint_change(
                        previous_project,
                        ConstraintKind::Path,
                        new_edit->name,
                        "Path constraint edit failed",
                        "Added path constraint " + new_edit->name);
                } else {
                    state->status_message = "Could not derive a valid default path constraint";
                }
            }

            ImGui::BeginChild("path_constraint_list", ImVec2(0.0f, 120.0f), true);
            for (const auto& constraint : skeleton.path_constraints()) {
                const bool selected =
                    selected_constraint(*state).has_value() &&
                    selected_constraint(*state)->kind == ConstraintKind::Path &&
                    selected_constraint(*state)->constraint_name == constraint.name;
                const bool has_project_edit =
                    project->find_path_constraint_edit(constraint.name) != nullptr;
                const std::string label =
                    constraint.name + (has_project_edit ? "  [project]" : "  [runtime]");
                if (icon_selectable(
                        state->icons,
                        Icon::ConstraintPath,
                        label.c_str(),
                        selected)) {
                    select_constraint(
                        state,
                        ConstraintKind::Path,
                        constraint.name,
                        "Constraints",
                        true);
                }
            }
            ImGui::EndChild();

            const std::string selected_name =
                selected_constraint(*state).has_value() &&
                    selected_constraint(*state)->kind == ConstraintKind::Path &&
                    find_named_constraint(skeleton.path_constraints(), selected_constraint(*state)->constraint_name) != nullptr
                ? selected_constraint(*state)->constraint_name
                : (!skeleton.path_constraints().empty() ? skeleton.path_constraints().front().name : std::string{});
            if (selected_name.empty()) {
                ImGui::TextUnformatted("No path constraints are active in the current runtime preview.");
            } else {
                const auto runtime_edit = make_path_constraint_edit_from_runtime(*state, selected_name);
                const marrow::editor::PathConstraintEdit* project_edit =
                    project->find_path_constraint_edit(selected_name);
                const marrow::editor::PathConstraintEdit display_edit =
                    project_edit != nullptr ? *project_edit : *runtime_edit;

                ImGui::Separator();
                ImGui::Text("Name: %s", display_edit.name.c_str());
                ImGui::Text(
                    "Source: %s",
                    project_edit != nullptr ? "project constraint edit" : "runtime constraint");

                std::string edited_slot = display_edit.slot_name;
                if (draw_string_combo("Guide Slot", path_slots, &edited_slot)) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        project->path_constraint_edits[*edit_index].slot_name = edited_slot;
                        commit_constraint_change(
                            previous_project,
                            ConstraintKind::Path,
                            project->path_constraint_edits[*edit_index].name,
                            "Path constraint edit failed",
                            "Updated guide slot on " + selected_name);
                    }
                }

                if (ImGui::Button("Add Chain Bone")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->path_constraint_edits[*edit_index];
                        if (append_direct_child_name(&edit.bone_names)) {
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Path,
                                edit.name,
                                "Path constraint edit failed",
                                "Extended the path chain on " + selected_name);
                        } else {
                            *project = previous_project;
                            state->status_message =
                                "Could not extend the path chain with a direct child bone";
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Chain Bone")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->path_constraint_edits[*edit_index];
                        if (edit.bone_names.size() > 1U) {
                            edit.bone_names.pop_back();
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Path,
                                edit.name,
                                "Path constraint edit failed",
                                "Shortened the path chain on " + selected_name);
                        }
                    }
                }

                for (std::size_t bone_index = 0; bone_index < display_edit.bone_names.size(); ++bone_index) {
                    std::string edited_bone = display_edit.bone_names[bone_index];
                    const std::string label = "Chain Bone " + std::to_string(bone_index + 1U);
                    if (draw_string_combo(label.c_str(), bone_options, &edited_bone)) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].bone_names[bone_index] =
                                edited_bone;
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Path,
                                project->path_constraint_edits[*edit_index].name,
                                "Path constraint edit failed",
                                "Updated path chain selection on " + selected_name);
                        }
                    }
                }

                double edited_position = display_edit.position;
                const bool position_changed = ImGui::SliderScalar(
                    "Position",
                    ImGuiDataType_Double,
                    &edited_position,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_constraint_project_drag(
                    state,
                    position_changed,
                    EditActionKind::EditProperty,
                    "Updated path position on " + selected_name,
                    constraint_group(ConstraintKind::Path, selected_name),
                    false,
                    "Path constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].position = edited_position;
                        }
                    });

                double edited_spacing = display_edit.spacing;
                const bool spacing_changed = ImGui::SliderScalar(
                    "Spacing",
                    ImGuiDataType_Double,
                    &edited_spacing,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_constraint_project_drag(
                    state,
                    spacing_changed,
                    EditActionKind::EditProperty,
                    "Updated path spacing on " + selected_name,
                    constraint_group(ConstraintKind::Path, selected_name),
                    false,
                    "Path constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].spacing = edited_spacing;
                        }
                    });

                int spacing_mode = display_edit.spacing_mode ==
                        marrow::runtime::PathConstraintSpacingMode::Percent
                    ? 1
                    : 0;
                constexpr const char* kSpacingModes[] = {"Length", "Percent"};
                if (ImGui::Combo(
                        "Spacing Mode",
                        &spacing_mode,
                        kSpacingModes,
                        IM_ARRAYSIZE(kSpacingModes))) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        project->path_constraint_edits[*edit_index].spacing_mode =
                            spacing_mode == 1 ? marrow::runtime::PathConstraintSpacingMode::Percent
                                              : marrow::runtime::PathConstraintSpacingMode::Length;
                        commit_constraint_change(
                            previous_project,
                            ConstraintKind::Path,
                            project->path_constraint_edits[*edit_index].name,
                            "Path constraint edit failed",
                            "Updated path spacing mode on " + selected_name);
                    }
                }

                double edited_rotate_mix = display_edit.rotate_mix;
                const bool rotate_mix_changed = ImGui::SliderScalar(
                    "Rotate Mix",
                    ImGuiDataType_Double,
                    &edited_rotate_mix,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_constraint_project_drag(
                    state,
                    rotate_mix_changed,
                    EditActionKind::EditProperty,
                    "Updated path rotate mix on " + selected_name,
                    constraint_group(ConstraintKind::Path, selected_name),
                    false,
                    "Path constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].rotate_mix =
                                edited_rotate_mix;
                        }
                    });

                double edited_translate_mix = display_edit.translate_mix;
                const bool translate_mix_changed = ImGui::SliderScalar(
                    "Translate Mix",
                    ImGuiDataType_Double,
                    &edited_translate_mix,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_constraint_project_drag(
                    state,
                    translate_mix_changed,
                    EditActionKind::EditProperty,
                    "Updated path translate mix on " + selected_name,
                    constraint_group(ConstraintKind::Path, selected_name),
                    false,
                    "Path constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].translate_mix =
                                edited_translate_mix;
                        }
                    });
            }

        }

        if (state->constraints_tab == 2) {
            if (ImGui::Button("Add Transform Constraint")) {
                const marrow::editor::ProjectData previous_project = *project;
                if (const auto new_edit = make_default_transform_constraint_edit(*state)) {
                    project->transform_constraint_edits.push_back(*new_edit);
                    commit_constraint_change(
                        previous_project,
                        ConstraintKind::Transform,
                        new_edit->name,
                        "Transform constraint edit failed",
                        "Added transform constraint " + new_edit->name);
                } else {
                    state->status_message = "Could not derive a valid default transform constraint";
                }
            }

            ImGui::BeginChild("transform_constraint_list", ImVec2(0.0f, 120.0f), true);
            for (const auto& constraint : skeleton.transform_constraints()) {
                const bool selected =
                    selected_constraint(*state).has_value() &&
                    selected_constraint(*state)->kind == ConstraintKind::Transform &&
                    selected_constraint(*state)->constraint_name == constraint.name;
                const bool has_project_edit =
                    project->find_transform_constraint_edit(constraint.name) != nullptr;
                const std::string label =
                    constraint.name + (has_project_edit ? "  [project]" : "  [runtime]");
                if (icon_selectable(
                        state->icons,
                        Icon::ConstraintXform,
                        label.c_str(),
                        selected)) {
                    select_constraint(
                        state,
                        ConstraintKind::Transform,
                        constraint.name,
                        "Constraints",
                        true);
                }
            }
            ImGui::EndChild();

            const std::string selected_name =
                selected_constraint(*state).has_value() &&
                    selected_constraint(*state)->kind == ConstraintKind::Transform &&
                    find_named_constraint(
                        skeleton.transform_constraints(),
                        selected_constraint(*state)->constraint_name) != nullptr
                ? selected_constraint(*state)->constraint_name
                : (!skeleton.transform_constraints().empty()
                       ? skeleton.transform_constraints().front().name
                       : std::string{});
            if (selected_name.empty()) {
                ImGui::TextUnformatted(
                    "No transform constraints are active in the current runtime preview.");
            } else {
                const auto runtime_edit =
                    make_transform_constraint_edit_from_runtime(*state, selected_name);
                const marrow::editor::TransformConstraintEdit* project_edit =
                    project->find_transform_constraint_edit(selected_name);
                const marrow::editor::TransformConstraintEdit display_edit =
                    project_edit != nullptr ? *project_edit : *runtime_edit;

                ImGui::Separator();
                ImGui::Text("Name: %s", display_edit.name.c_str());
                ImGui::Text(
                    "Source: %s",
                    project_edit != nullptr ? "project constraint edit" : "runtime constraint");

                std::string edited_source = display_edit.source_bone_name;
                if (draw_string_combo("Source Bone", bone_options, &edited_source)) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index =
                            ensure_transform_constraint_edit_index(state, selected_name)) {
                        project->transform_constraint_edits[*edit_index].source_bone_name =
                            edited_source;
                        commit_constraint_change(
                            previous_project,
                            ConstraintKind::Transform,
                            project->transform_constraint_edits[*edit_index].name,
                            "Transform constraint edit failed",
                            "Updated transform source on " + selected_name);
                    }
                }

                if (ImGui::Button("Add Target Bone")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index =
                            ensure_transform_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->transform_constraint_edits[*edit_index];
                        const auto candidate = first_constraint_target_name(
                            skeleton,
                            [&]() {
                                std::vector<std::string> excluded = edit.bone_names;
                                excluded.push_back(edit.source_bone_name);
                                return excluded;
                            }());
                        if (candidate.has_value()) {
                            edit.bone_names.push_back(*candidate);
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Transform,
                                edit.name,
                                "Transform constraint edit failed",
                                "Added a transform target on " + selected_name);
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Target Bone")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index =
                            ensure_transform_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->transform_constraint_edits[*edit_index];
                        if (edit.bone_names.size() > 1U) {
                            edit.bone_names.pop_back();
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Transform,
                                edit.name,
                                "Transform constraint edit failed",
                                "Removed a transform target on " + selected_name);
                        }
                    }
                }

                for (std::size_t bone_index = 0; bone_index < display_edit.bone_names.size(); ++bone_index) {
                    std::string edited_bone = display_edit.bone_names[bone_index];
                    const std::string label = "Target Bone " + std::to_string(bone_index + 1U);
                    if (draw_string_combo(label.c_str(), bone_options, &edited_bone)) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index =
                                ensure_transform_constraint_edit_index(state, selected_name)) {
                            project->transform_constraint_edits[*edit_index].bone_names[bone_index] =
                                edited_bone;
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Transform,
                                project->transform_constraint_edits[*edit_index].name,
                                "Transform constraint edit failed",
                                "Updated transform target selection on " + selected_name);
                        }
                    }
                }

                const auto update_mix = [&](const char* label,
                                            double value,
                                            auto setter,
                                            std::string status) {
                    double edited_value = value;
                    const bool changed = ImGui::SliderScalar(
                        label,
                        ImGuiDataType_Double,
                        &edited_value,
                        &kZero,
                        &kOne,
                        "%.2f");
                    apply_constraint_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        constraint_group(ConstraintKind::Transform, selected_name),
                        false,
                        "Transform constraint edit failed",
                        [&]() {
                            if (const auto edit_index =
                                    ensure_transform_constraint_edit_index(state, selected_name)) {
                                setter(&project->transform_constraint_edits[*edit_index], edited_value);
                            }
                        });
                };
                update_mix(
                    "Rotate Mix",
                    display_edit.rotate_mix,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->rotate_mix = value;
                    },
                    "Updated transform rotate mix on " + selected_name);
                update_mix(
                    "Translate Mix",
                    display_edit.translate_mix,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->translate_mix = value;
                    },
                    "Updated transform translate mix on " + selected_name);
                update_mix(
                    "Scale Mix",
                    display_edit.scale_mix,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->scale_mix = value;
                    },
                    "Updated transform scale mix on " + selected_name);
                update_mix(
                    "Shear Mix",
                    display_edit.shear_mix,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->shear_mix = value;
                    },
                    "Updated transform shear mix on " + selected_name);

                const auto update_offset = [&](const char* label,
                                               double value,
                                               auto setter,
                                               std::string status) {
                    double edited_value = value;
                    const bool changed = ImGui::DragScalar(
                        label,
                        ImGuiDataType_Double,
                        &edited_value,
                        0.1f,
                        nullptr,
                        nullptr,
                        "%.3f");
                    apply_constraint_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        constraint_group(ConstraintKind::Transform, selected_name),
                        false,
                        "Transform constraint edit failed",
                        [&]() {
                            if (const auto edit_index =
                                    ensure_transform_constraint_edit_index(state, selected_name)) {
                                setter(&project->transform_constraint_edits[*edit_index], edited_value);
                            }
                        });
                };
                update_offset(
                    "Offset Rotation",
                    display_edit.offsets.rotation,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.rotation = value;
                    },
                    "Updated transform rotation offset on " + selected_name);
                update_offset(
                    "Offset X",
                    display_edit.offsets.x,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.x = value;
                    },
                    "Updated transform X offset on " + selected_name);
                update_offset(
                    "Offset Y",
                    display_edit.offsets.y,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.y = value;
                    },
                    "Updated transform Y offset on " + selected_name);
                update_offset(
                    "Offset Scale X",
                    display_edit.offsets.scale_x,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.scale_x = value;
                    },
                    "Updated transform scaleX offset on " + selected_name);
                update_offset(
                    "Offset Scale Y",
                    display_edit.offsets.scale_y,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.scale_y = value;
                    },
                    "Updated transform scaleY offset on " + selected_name);
                update_offset(
                    "Offset Shear X",
                    display_edit.offsets.shear_x,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.shear_x = value;
                    },
                    "Updated transform shearX offset on " + selected_name);
                update_offset(
                    "Offset Shear Y",
                    display_edit.offsets.shear_y,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.shear_y = value;
                    },
                    "Updated transform shearY offset on " + selected_name);
            }

        }

        if (state->constraints_tab == 3) {
            if (ImGui::Button("Add Physics Constraint")) {
                const marrow::editor::ProjectData previous_project = *project;
                if (const auto new_edit = make_default_physics_constraint_edit(*state)) {
                    project->physics_constraint_edits.push_back(*new_edit);
                    commit_constraint_change(
                        previous_project,
                        ConstraintKind::Physics,
                        new_edit->name,
                        "Physics constraint edit failed",
                        "Added physics constraint " + new_edit->name);
                } else {
                    state->status_message = "Could not derive a valid default physics constraint";
                }
            }

            ImGui::BeginChild("physics_constraint_list", ImVec2(0.0f, 120.0f), true);
            for (const auto& constraint : skeleton.physics_constraints()) {
                const bool selected =
                    selected_constraint(*state).has_value() &&
                    selected_constraint(*state)->kind == ConstraintKind::Physics &&
                    selected_constraint(*state)->constraint_name == constraint.name;
                const bool has_project_edit =
                    project->find_physics_constraint_edit(constraint.name) != nullptr;
                const std::string label =
                    constraint.name + (has_project_edit ? "  [project]" : "  [runtime]");
                if (icon_selectable(
                        state->icons,
                        Icon::ConstraintPhysics,
                        label.c_str(),
                        selected)) {
                    select_constraint(
                        state,
                        ConstraintKind::Physics,
                        constraint.name,
                        "Constraints",
                        true);
                }
            }
            ImGui::EndChild();

            const std::string selected_name =
                selected_constraint(*state).has_value() &&
                    selected_constraint(*state)->kind == ConstraintKind::Physics &&
                    find_named_constraint(
                        skeleton.physics_constraints(),
                        selected_constraint(*state)->constraint_name) != nullptr
                ? selected_constraint(*state)->constraint_name
                : (!skeleton.physics_constraints().empty()
                       ? skeleton.physics_constraints().front().name
                       : std::string{});
            if (selected_name.empty()) {
                ImGui::TextUnformatted(
                    "No physics constraints are active in the current runtime preview.");
            } else {
                const auto runtime_edit =
                    make_physics_constraint_edit_from_runtime(*state, selected_name);
                const marrow::editor::PhysicsConstraintEdit* project_edit =
                    project->find_physics_constraint_edit(selected_name);
                const marrow::editor::PhysicsConstraintEdit display_edit =
                    project_edit != nullptr ? *project_edit : *runtime_edit;

                ImGui::Separator();
                ImGui::Text("Name: %s", display_edit.name.c_str());
                ImGui::Text(
                    "Source: %s",
                    project_edit != nullptr ? "project constraint edit" : "runtime constraint");

                if (ImGui::Button("Add Chain Bone##physics")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_physics_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->physics_constraint_edits[*edit_index];
                        if (append_direct_child_name(&edit.bone_names)) {
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Physics,
                                edit.name,
                                "Physics constraint edit failed",
                                "Extended the physics chain on " + selected_name);
                        } else {
                            *project = previous_project;
                            state->status_message =
                                "Could not extend the physics chain with a direct child bone";
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Chain Bone##physics")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_physics_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->physics_constraint_edits[*edit_index];
                        if (edit.bone_names.size() > 1U) {
                            edit.bone_names.pop_back();
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Physics,
                                edit.name,
                                "Physics constraint edit failed",
                                "Shortened the physics chain on " + selected_name);
                        }
                    }
                }

                for (std::size_t bone_index = 0; bone_index < display_edit.bone_names.size(); ++bone_index) {
                    std::string edited_bone = display_edit.bone_names[bone_index];
                    const std::string label = "Chain Bone " + std::to_string(bone_index + 1U) +
                        "##physics";
                    if (draw_string_combo(label.c_str(), bone_options, &edited_bone)) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index = ensure_physics_constraint_edit_index(state, selected_name)) {
                            project->physics_constraint_edits[*edit_index].bone_names[bone_index] =
                                edited_bone;
                            commit_constraint_change(
                                previous_project,
                                ConstraintKind::Physics,
                                project->physics_constraint_edits[*edit_index].name,
                                "Physics constraint edit failed",
                                "Updated physics chain selection on " + selected_name);
                        }
                    }
                }

                auto update_positive_value = [&](const char* label,
                                                 double value,
                                                 auto setter,
                                                 double max_value,
                                                 std::string status) {
                    double edited_value = value;
                    const bool changed = ImGui::SliderScalar(
                        label,
                        ImGuiDataType_Double,
                        &edited_value,
                        &kZero,
                        &max_value,
                        "%.2f");
                    apply_constraint_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        constraint_group(ConstraintKind::Physics, selected_name),
                        false,
                        "Physics constraint edit failed",
                        [&]() {
                            if (const auto edit_index =
                                    ensure_physics_constraint_edit_index(state, selected_name)) {
                                setter(&project->physics_constraint_edits[*edit_index], edited_value);
                            }
                        });
                };
                update_positive_value(
                    "Inertia",
                    display_edit.inertia,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->inertia = value;
                    },
                    kOne,
                    "Updated physics inertia on " + selected_name);
                update_positive_value(
                    "Damping",
                    display_edit.damping,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->damping = value;
                    },
                    kTen,
                    "Updated physics damping on " + selected_name);
                update_positive_value(
                    "Strength",
                    display_edit.strength,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->strength = value;
                    },
                    50.0,
                    "Updated physics strength on " + selected_name);
                update_positive_value(
                    "Mix##physics",
                    display_edit.mix,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->mix = value;
                    },
                    kOne,
                    "Updated physics mix on " + selected_name);

                const auto update_force = [&](const char* label,
                                              float value,
                                              auto setter,
                                              std::string status) {
                    float edited_value = value;
                    const bool changed = ImGui::DragScalar(
                        label,
                        ImGuiDataType_Float,
                        &edited_value,
                        0.5f,
                        nullptr,
                        nullptr,
                        "%.2f");
                    apply_constraint_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        constraint_group(ConstraintKind::Physics, selected_name),
                        false,
                        "Physics constraint edit failed",
                        [&]() {
                            if (const auto edit_index =
                                    ensure_physics_constraint_edit_index(state, selected_name)) {
                                setter(&project->physics_constraint_edits[*edit_index], edited_value);
                            }
                        });
                };
                update_force(
                    "Gravity X",
                    display_edit.gravity.x,
                    [](marrow::editor::PhysicsConstraintEdit* edit, float value) {
                        edit->gravity.x = value;
                    },
                    "Updated physics gravity X on " + selected_name);
                update_force(
                    "Gravity Y",
                    display_edit.gravity.y,
                    [](marrow::editor::PhysicsConstraintEdit* edit, float value) {
                        edit->gravity.y = value;
                    },
                    "Updated physics gravity Y on " + selected_name);
                update_force(
                    "Wind X",
                    display_edit.wind.x,
                    [](marrow::editor::PhysicsConstraintEdit* edit, float value) {
                        edit->wind.x = value;
                    },
                    "Updated physics wind X on " + selected_name);
                update_force(
                    "Wind Y",
                    display_edit.wind.y,
                    [](marrow::editor::PhysicsConstraintEdit* edit, float value) {
                        edit->wind.y = value;
                    },
                    "Updated physics wind Y on " + selected_name);
            }

        }
    }

    ImGui::End();
}


} // namespace marrow::editor::shell
