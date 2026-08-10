#include "shell_selection.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_derived_cache.hpp"
#include "shell_theme.hpp"
#include "shell_preview.hpp"
#include "shell_widgets.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;
using marrow::editor::IconRegistry;

namespace {

std::string selection_item_label(
    const marrow::editor::SelectionItem& item) {
    return std::visit(
        [](const auto& selection) {
            using Selection = std::decay_t<decltype(selection)>;
            if constexpr (std::is_same_v<Selection, marrow::editor::BoneSelection>) {
                return std::string("bone ") + selection.bone_name;
            } else if constexpr (
                std::is_same_v<Selection, marrow::editor::SlotSelection>) {
                return std::string("slot ") + selection.slot_name;
            } else if constexpr (
                std::is_same_v<Selection, marrow::editor::AttachmentSelection>) {
                return std::string("attachment ") + selection.slot_name + "/" +
                    selection.skin_name + "/" + selection.attachment_name;
            } else {
                std::string kind;
                switch (selection.kind) {
                case marrow::editor::ConstraintKind::Ik:
                    kind = "IK";
                    break;
                case marrow::editor::ConstraintKind::Path:
                    kind = "Path";
                    break;
                case marrow::editor::ConstraintKind::Transform:
                    kind = "Transform";
                    break;
                case marrow::editor::ConstraintKind::Physics:
                    kind = "Physics";
                    break;
                }
                return kind + " constraint " + selection.constraint_name;
            }
        },
        item);
}

} // namespace

ResolvedSelection resolve_shell_selection(const ShellState& state) {
    ResolvedSelection resolved;
    if (!state.load_result) {
        return resolved;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    if (const auto* bone = state.selection.active_bone()) {
        resolved.active_bone_index = skeleton.find_bone_index(bone->bone_name);
        resolved.context_bone_index = resolved.active_bone_index;
        return resolved;
    }
    if (const auto* slot = state.selection.active_slot()) {
        resolved.active_slot_index = skeleton.find_slot_index(slot->slot_name);
        if (resolved.active_slot_index.has_value() &&
            *resolved.active_slot_index < skeleton.slots().size()) {
            resolved.context_bone_index =
                skeleton.slots()[*resolved.active_slot_index].bone_index;
            resolved.attachment_context =
                current_attachment_selection(state, *resolved.active_slot_index);
        }
        return resolved;
    }
    if (const auto* attachment = state.selection.active_attachment()) {
        const auto slot_index = skeleton.find_slot_index(attachment->slot_name);
        const auto skin_index = skeleton.find_skin_index(attachment->skin_name);
        if (slot_index.has_value() && skin_index.has_value() &&
            skeleton.find_attachment(
                *skin_index,
                *slot_index,
                attachment->attachment_name) != nullptr) {
            resolved.active_slot_index = slot_index;
            resolved.context_bone_index = skeleton.slots()[*slot_index].bone_index;
            resolved.active_attachment = PreviewAttachmentSelection{
                *slot_index,
                *skin_index,
                attachment->attachment_name};
            resolved.attachment_context = resolved.active_attachment;
        }
        return resolved;
    }
    if (const auto* constraint = state.selection.active_constraint();
        constraint != nullptr &&
        marrow::editor::selection_item_exists(*constraint, skeleton)) {
        resolved.active_constraint = *constraint;
    }
    return resolved;
}

bool hierarchy_command_modifier(
    bool config_macosx_behaviors,
    bool key_ctrl,
    bool key_super) noexcept {
    return config_macosx_behaviors ? key_super : key_ctrl;
}

bool reconcile_hierarchy_anchor_visibility(
    ShellState* state,
    const std::vector<marrow::editor::SelectionItem>& visible_items) {
    if (state == nullptr || !state->hierarchy_selection_anchor.has_value() ||
        std::find(
            visible_items.begin(),
            visible_items.end(),
            *state->hierarchy_selection_anchor) != visible_items.end()) {
        return false;
    }

    state->hierarchy_selection_anchor.reset();
    return true;
}

bool reconcile_hierarchy_anchor_to_runtime(
    ShellState* state,
    const marrow::runtime::SkeletonData& skeleton) {
    if (state == nullptr || !state->hierarchy_selection_anchor.has_value() ||
        marrow::editor::selection_item_exists(
            *state->hierarchy_selection_anchor,
            skeleton)) {
        return false;
    }

    state->hierarchy_selection_anchor.reset();
    return true;
}

HierarchyRowSelectionState hierarchy_row_selection_state(
    const ShellState& state,
    const marrow::editor::SelectionItem& item) noexcept {
    const marrow::editor::SelectionItem* active = state.selection.active();
    if (active != nullptr && *active == item) {
        return HierarchyRowSelectionState::Active;
    }
    return state.selection.contains(item)
        ? HierarchyRowSelectionState::Selected
        : HierarchyRowSelectionState::Unselected;
}

bool apply_hierarchy_selection_gesture(
    ShellState* state,
    const std::vector<marrow::editor::SelectionItem>& visible_items,
    const marrow::editor::SelectionItem& clicked_item,
    HierarchySelectionModifiers modifiers,
    bool update_status_message) {
    if (state == nullptr) {
        return false;
    }

    const auto clicked_iterator = std::find(
        visible_items.begin(), visible_items.end(), clicked_item);
    if (clicked_iterator == visible_items.end()) {
        return false;
    }

    const std::vector<marrow::editor::SelectionItem> selection_before =
        state->selection.items();
    const std::optional<marrow::editor::SelectionItem> active_before =
        state->selection.active() != nullptr
        ? std::optional<marrow::editor::SelectionItem>(*state->selection.active())
        : std::nullopt;
    const std::optional<marrow::editor::SelectionItem> anchor_before =
        state->hierarchy_selection_anchor;
    const bool timeline_focus_before = state->selected_timeline_track_id.has_value();

    auto anchor_iterator = visible_items.end();
    if (state->hierarchy_selection_anchor.has_value()) {
        anchor_iterator = std::find(
            visible_items.begin(),
            visible_items.end(),
            *state->hierarchy_selection_anchor);
    }
    const bool valid_anchor = anchor_iterator != visible_items.end();

    if (!modifiers.shift) {
        if (modifiers.command) {
            (void)state->selection.toggle(clicked_item);
        } else {
            (void)state->selection.replace(clicked_item);
        }
        state->hierarchy_selection_anchor = clicked_item;
    } else if (!valid_anchor) {
        if (modifiers.command) {
            (void)state->selection.add_range({clicked_item}, clicked_item);
        } else {
            (void)state->selection.replace(clicked_item);
        }
        state->hierarchy_selection_anchor = clicked_item;
    } else {
        const std::size_t anchor_index = static_cast<std::size_t>(
            std::distance(visible_items.begin(), anchor_iterator));
        const std::size_t clicked_index = static_cast<std::size_t>(
            std::distance(visible_items.begin(), clicked_iterator));
        const std::size_t range_begin = std::min(anchor_index, clicked_index);
        const std::size_t range_end = std::max(anchor_index, clicked_index);
        const std::vector<marrow::editor::SelectionItem> range_items(
            visible_items.begin() + static_cast<std::ptrdiff_t>(range_begin),
            visible_items.begin() + static_cast<std::ptrdiff_t>(range_end + 1U));

        if (modifiers.command) {
            (void)state->selection.add_range(range_items, clicked_item);
        } else {
            marrow::editor::SelectionSet replacement;
            (void)replacement.add_range(range_items, clicked_item);
            state->selection = std::move(replacement);
        }
    }

    state->selected_timeline_track_id.reset();
    if (update_status_message) {
        std::ostringstream stream;
        stream << "Hierarchy selection: ";
        if (const auto* active = state->selection.active()) {
            stream << "active " << selection_item_label(*active);
        } else {
            stream << "no active item";
        }
        stream << "; " << state->selection.items().size() << " selected";
        state->status_message = stream.str();
    }

    const std::optional<marrow::editor::SelectionItem> active_after =
        state->selection.active() != nullptr
        ? std::optional<marrow::editor::SelectionItem>(*state->selection.active())
        : std::nullopt;
    return state->selection.items() != selection_before ||
        active_after != active_before ||
        state->hierarchy_selection_anchor != anchor_before ||
        timeline_focus_before;
}

bool apply_viewport_point_selection_gesture(
    ShellState* state,
    const marrow::editor::SelectionItem& clicked_item,
    bool command_modifier,
    bool update_status_message) {
    if (state == nullptr || !state->load_result ||
        !marrow::editor::selection_item_exists(
            clicked_item, *state->load_result.skeleton_data)) {
        return false;
    }

    const std::vector<marrow::editor::SelectionItem> selection_before =
        state->selection.items();
    const std::optional<marrow::editor::SelectionItem> active_before =
        state->selection.active() != nullptr
        ? std::optional<marrow::editor::SelectionItem>(*state->selection.active())
        : std::nullopt;
    const bool had_anchor = state->hierarchy_selection_anchor.has_value();
    const bool had_timeline_focus = state->selected_timeline_track_id.has_value();

    if (command_modifier) {
        (void)state->selection.toggle(clicked_item);
    } else {
        (void)state->selection.replace(clicked_item);
    }
    state->hierarchy_selection_anchor.reset();
    state->selected_timeline_track_id.reset();

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Viewport selection: ";
        if (const auto* active = state->selection.active()) {
            stream << "active " << selection_item_label(*active);
        } else {
            stream << "no active item";
        }
        stream << "; " << state->selection.items().size() << " selected";
        state->status_message = stream.str();
    }

    const std::optional<marrow::editor::SelectionItem> active_after =
        state->selection.active() != nullptr
        ? std::optional<marrow::editor::SelectionItem>(*state->selection.active())
        : std::nullopt;
    return state->selection.items() != selection_before ||
        active_after != active_before || had_anchor || had_timeline_focus;
}

bool apply_viewport_box_selection_gesture(
    ShellState* state,
    const std::vector<marrow::editor::SelectionItem>& ordered_bones,
    bool additive,
    bool update_status_message) {
    if (state == nullptr || !state->load_result ||
        std::any_of(
            ordered_bones.begin(),
            ordered_bones.end(),
            [&](const marrow::editor::SelectionItem& item) {
                return std::get_if<marrow::editor::BoneSelection>(&item) == nullptr ||
                    !marrow::editor::selection_item_exists(
                        item, *state->load_result.skeleton_data);
            })) {
        return false;
    }

    const std::vector<marrow::editor::SelectionItem> selection_before =
        state->selection.items();
    const std::optional<marrow::editor::SelectionItem> active_before =
        state->selection.active() != nullptr
        ? std::optional<marrow::editor::SelectionItem>(*state->selection.active())
        : std::nullopt;
    const bool had_anchor = state->hierarchy_selection_anchor.has_value();
    const bool had_timeline_focus = state->selected_timeline_track_id.has_value();

    if (!ordered_bones.empty()) {
        const marrow::editor::SelectionItem& active_bone = ordered_bones.back();
        if (additive) {
            (void)state->selection.add_range(ordered_bones, active_bone);
        } else {
            marrow::editor::SelectionSet replacement;
            (void)replacement.add_range(ordered_bones, active_bone);
            state->selection = std::move(replacement);
        }
    } else if (!additive) {
        (void)state->selection.clear();
    }

    const bool selection_changed = state->selection.items() != selection_before ||
        (state->selection.active() != nullptr
            ? std::optional<marrow::editor::SelectionItem>(*state->selection.active())
            : std::nullopt) != active_before;
    if (!selection_changed && additive) {
        return false;
    }

    state->hierarchy_selection_anchor.reset();
    state->selected_timeline_track_id.reset();
    if (update_status_message) {
        std::ostringstream stream;
        stream << "Viewport box selection: ";
        if (const auto* active = state->selection.active()) {
            stream << "active " << selection_item_label(*active);
        } else {
            stream << "no active item";
        }
        stream << "; " << state->selection.items().size() << " selected";
        state->status_message = stream.str();
    }
    return selection_changed || had_anchor || had_timeline_focus;
}

std::optional<std::size_t> selected_slot_index(const ShellState& state) {
    return resolve_shell_selection(state).active_slot_index;
}

std::optional<std::size_t> selected_bone_index(const ShellState& state) {
    return resolve_shell_selection(state).context_bone_index;
}

std::optional<std::string_view> selected_bone_name(const ShellState& state) {
    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (!state.load_result || !resolved.context_bone_index.has_value() ||
        *resolved.context_bone_index >= state.load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }
    return state.load_result.skeleton_data->bones()[*resolved.context_bone_index].name;
}

std::optional<PreviewAttachmentSelection> selected_attachment(const ShellState& state) {
    return resolve_shell_selection(state).attachment_context;
}

std::optional<marrow::editor::ConstraintSelection> selected_constraint(
    const ShellState& state) {
    return resolve_shell_selection(state).active_constraint;
}

std::string join_strings(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "<none>";
    }

    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << values[index];
    }
    return stream.str();
}

void select_bone(
    ShellState* state,
    std::optional<std::size_t> bone_index,
    std::string_view source,
    bool update_status_message) {
    state->hierarchy_selection_anchor.reset();
    if (!bone_index.has_value() || !state->load_result ||
        *bone_index >= state->load_result.skeleton_data->bones().size()) {
        state->selection.clear();
        return;
    }

    const auto& bones = state->load_result.skeleton_data->bones();
    state->selection.replace(marrow::editor::BoneSelection{bones[*bone_index].name});
    state->selected_timeline_track_id.reset();
    if (!update_status_message) {
        return;
    }

    std::ostringstream stream;
    stream << "Selected bone " << bones[*bone_index].name;
    if (!source.empty()) {
        stream << " via " << source;
    }
    state->status_message = stream.str();
}

void select_attachment(
    ShellState* state,
    std::optional<PreviewAttachmentSelection> selection,
    std::string_view source,
    bool update_status_message) {
    state->hierarchy_selection_anchor.reset();
    if (!selection.has_value() || !state->load_result) {
        state->selection.clear();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const auto reference = resolve_attachment_reference(skeleton, *selection);
    if (!reference.has_value() || !reference->skin_index.has_value() ||
        selection->slot_index >= skeleton.slots().size() ||
        *reference->skin_index >= skeleton.skins().size()) {
        state->selection.clear();
        return;
    }
    state->selection.replace(marrow::editor::AttachmentSelection{
        skeleton.slots()[selection->slot_index].name,
        skeleton.skins()[*reference->skin_index].name,
        reference->attachment->name});
    state->selected_timeline_track_id.reset();

    if (!update_status_message) {
        return;
    }

    std::ostringstream stream;
    stream << "Selected attachment " << selection->attachment_name;
    if (selection->skin_index.has_value()) {
        stream << " from skin "
               << source_skin_name(*state->load_result.skeleton_data, selection->skin_index);
    }
    if (!source.empty()) {
        stream << " via " << source;
    }
    state->status_message = stream.str();
}

void select_slot(
    ShellState* state,
    std::optional<std::size_t> slot_index,
    std::string_view source,
    bool update_status_message) {
    state->hierarchy_selection_anchor.reset();
    if (!slot_index.has_value() || !state->load_result ||
        *slot_index >= state->load_result.skeleton_data->slots().size()) {
        state->selection.clear();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    state->selection.replace(marrow::editor::SlotSelection{
        skeleton.slots()[*slot_index].name});
    state->selected_timeline_track_id.reset();

    if (!update_status_message) {
        return;
    }

    std::ostringstream stream;
    stream << "Selected slot " << skeleton.slots()[*slot_index].name;
    if (!source.empty()) {
        stream << " via " << source;
    }
    state->status_message = stream.str();
}

bool apply_preview_skin_selection(
    ShellState* state,
    std::string_view source,
    bool update_status_message) {
    if (!state->load_result || !state->preview_skeleton) {
        return false;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    state->preview_skin_names =
        normalize_preview_skin_names(skeleton, state->preview_skin_names);

    if (!refresh_preview_pose(state)) {
        return false;
    }

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Preview skins: "
               << preview_skin_summary(skeleton, state->preview_skin_names);
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}


template <typename MutateFn>
bool execute_preview_edit_action(
    ShellState* state,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    std::string failure_status,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before changing preview composition";
        return false;
    }

    const EditorHistorySnapshot before = capture_history_snapshot(*state);
    mutate();
    if (!refresh_preview_pose(state)) {
        const std::string rebuild_error = state->error_message;
        restore_history_snapshot(state, before);
        state->error_message = rebuild_error;
        state->status_message = std::move(failure_status);
        return false;
    }

    return record_action_from_snapshots(
        state,
        before,
        kind,
        std::move(label),
        std::move(group),
        allow_merge);
}


bool set_preview_skin_enabled(
    ShellState* state,
    std::size_t skin_index,
    bool enabled,
    bool update_status_message,
    bool record_history) {
    if (!state->load_result || skin_index >= state->load_result.skeleton_data->skins().size()) {
        return false;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    if (is_default_skin_index(skeleton, skin_index)) {
        return false;
    }

    const std::string& skin_name = skeleton.skins()[skin_index].name;
    const auto existing = std::find(
        state->preview_skin_names.begin(),
        state->preview_skin_names.end(),
        skin_name);
    const bool already_enabled = existing != state->preview_skin_names.end();
    if (already_enabled == enabled) {
        if (update_status_message) {
            state->status_message =
                std::string(enabled ? "Preview skin already enabled: "
                                    : "Preview skin already disabled: ") +
                skin_name;
        }
        return true;
    }

    if (!record_history) {
        if (enabled) {
            state->preview_skin_names.push_back(skin_name);
        } else {
            state->preview_skin_names.erase(existing);
        }
        return apply_preview_skin_selection(state, "Skin Preview", update_status_message);
    }

    return execute_preview_edit_action(
        state,
        EditActionKind::EditProperty,
        std::string(enabled ? "Enabled preview skin " : "Disabled preview skin ") + skin_name,
        "preview-skins",
        true,
        "Preview skin change failed",
        [&]() {
            if (enabled) {
                state->preview_skin_names.push_back(skin_name);
            } else {
                state->preview_skin_names.erase(existing);
            }
        });
}

bool apply_attachment_selection_to_preview_slot(
    ShellState* state,
    const PreviewAttachmentSelection& selection,
    std::string_view source,
    bool update_status_message,
    bool record_history) {
    if (!state->load_result || !state->preview_skeleton ||
        selection.slot_index >= state->preview_skeleton->slot_states().size()) {
        return false;
    }

    if (!resolve_attachment_reference(*state->load_result.skeleton_data, selection).has_value()) {
        return false;
    }

    const auto apply_selection = [&]() {
        if (selection.slot_index >= state->preview_slot_overrides.size()) {
            state->preview_slot_overrides.resize(state->preview_skeleton->slot_states().size());
        }
        state->preview_slot_overrides[selection.slot_index] = selection;
        select_attachment(state, selection, source, false);
    };

    if (!record_history) {
        apply_selection();
        if (!refresh_preview_pose(state)) {
            return false;
        }

        if (update_status_message) {
            std::ostringstream stream;
            stream << "Preview slot "
                   << state->load_result.skeleton_data->slots()[selection.slot_index].name
                   << " set to " << selection.attachment_name;
            if (!source.empty()) {
                stream << " via " << source;
            }
            state->status_message = stream.str();
        }

        return true;
    }

    const std::string slot_name =
        state->load_result.skeleton_data->slots()[selection.slot_index].name;
    return execute_preview_edit_action(
        state,
        EditActionKind::EditProperty,
        "Swapped preview attachment on " + slot_name + " to " + selection.attachment_name,
        "preview-slot:" + slot_name,
        true,
        "Preview attachment swap failed",
        apply_selection);
}

bool reset_preview_slot_to_skin_selection(
    ShellState* state,
    std::size_t slot_index,
    std::string_view source,
    bool update_status_message,
    bool record_history) {
    if (!state->load_result || !state->preview_skeleton ||
        slot_index >= state->preview_skeleton->slot_states().size()) {
        return false;
    }

    const auto reset_selection = [&]() {
        if (slot_index >= state->preview_slot_overrides.size()) {
            state->preview_slot_overrides.resize(state->preview_skeleton->slot_states().size());
        }
        state->preview_slot_overrides[slot_index].reset();
        if (const auto preview_selection = current_attachment_selection(*state, slot_index)) {
            select_attachment(state, preview_selection, source, false);
        } else {
            select_attachment(state, std::nullopt, source, false);
        }
    };

    if (!record_history) {
        reset_selection();
        if (!refresh_preview_pose(state)) {
            return false;
        }

        if (update_status_message) {
            std::ostringstream stream;
            stream << "Reset preview slot "
                   << state->load_result.skeleton_data->slots()[slot_index].name
                   << " to the active skin composition";
            if (!source.empty()) {
                stream << " via " << source;
            }
            state->status_message = stream.str();
        }

        return true;
    }

    const std::string slot_name = state->load_result.skeleton_data->slots()[slot_index].name;
    return execute_preview_edit_action(
        state,
        EditActionKind::EditProperty,
        "Reset preview slot " + slot_name + " to the active skin composition",
        "preview-slot:" + slot_name,
        true,
        "Preview attachment reset failed",
        reset_selection);
}


std::vector<std::vector<std::size_t>> build_bone_children(
    const marrow::runtime::SkeletonData& skeleton) {
    std::vector<std::vector<std::size_t>> children(skeleton.bones().size());
    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        const auto& bone = skeleton.bones()[bone_index];
        if (bone.parent_index.has_value() && *bone.parent_index < children.size()) {
            children[*bone.parent_index].push_back(bone_index);
        }
    }
    return children;
}


std::optional<std::string_view> default_skin_name(
    const marrow::runtime::SkeletonData& skeleton) {
    const auto default_skin_index = skeleton.default_skin_index();
    if (!default_skin_index.has_value() || *default_skin_index >= skeleton.skins().size()) {
        return std::nullopt;
    }

    return skeleton.skins()[*default_skin_index].name;
}

bool is_default_skin_index(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t skin_index) {
    const auto default_skin_index = skeleton.default_skin_index();
    return default_skin_index.has_value() && *default_skin_index == skin_index;
}

std::string source_skin_name(
    const marrow::runtime::SkeletonData& skeleton,
    std::optional<std::size_t> skin_index) {
    if (!skin_index.has_value() || *skin_index >= skeleton.skins().size()) {
        return "<unresolved>";
    }

    return skeleton.skins()[*skin_index].name;
}


std::string preview_skin_summary(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names) {
    std::vector<std::string> labels;
    if (const auto default_name = default_skin_name(skeleton)) {
        labels.push_back(std::string(*default_name));
    }
    labels.insert(labels.end(), preview_skin_names.begin(), preview_skin_names.end());
    return join_strings(labels);
}

std::vector<SlotAttachmentReference> collect_slot_attachments(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index) {
    std::vector<SlotAttachmentReference> attachments;
    for (std::size_t skin_index = 0; skin_index < skeleton.skins().size(); ++skin_index) {
        const auto& skin = skeleton.skins()[skin_index];
        for (const auto& slot_attachment : skin.slot_attachments) {
            if (slot_attachment.slot_index != slot_index) {
                continue;
            }

            attachments.push_back(
                SlotAttachmentReference{slot_index, skin_index, &slot_attachment.attachment});
        }
    }
    return attachments;
}

std::optional<SlotAttachmentReference> resolve_attachment_reference(
    const marrow::runtime::SkeletonData& skeleton,
    const PreviewAttachmentSelection& selection) {
    if (selection.slot_index >= skeleton.slots().size()) {
        return std::nullopt;
    }

    if (selection.skin_index.has_value()) {
        const marrow::runtime::AttachmentData* attachment =
            skeleton.find_attachment(
                *selection.skin_index,
                selection.slot_index,
                selection.attachment_name);
        if (attachment != nullptr) {
            return SlotAttachmentReference{
                selection.slot_index,
                selection.skin_index,
                attachment};
        }
        return std::nullopt;
    }

    std::optional<std::size_t> source_skin_index;
    const marrow::runtime::AttachmentData* attachment = skeleton.find_attachment_source(
        selection.slot_index,
        selection.attachment_name,
        &source_skin_index);
    if (attachment == nullptr) {
        return std::nullopt;
    }

    return SlotAttachmentReference{
        selection.slot_index,
        source_skin_index,
        attachment};
}

std::optional<PreviewAttachmentSelection> current_attachment_selection(
    const ShellState& state,
    std::size_t slot_index) {
    if (!state.load_result || !state.preview_skeleton ||
        slot_index >= state.preview_skeleton->slot_states().size()) {
        return std::nullopt;
    }

    const auto& slot_state = state.preview_skeleton->slot_states()[slot_index];
    if (slot_state.attachment_name.empty()) {
        return std::nullopt;
    }

    std::optional<std::size_t> source_skin_index = slot_state.attachment_skin_index;
    state.load_result.skeleton_data->find_attachment_source(
        slot_index,
        slot_state.attachment_name,
        &source_skin_index);
    return PreviewAttachmentSelection{
        slot_index,
        source_skin_index,
        slot_state.attachment_name};
}


std::vector<std::size_t> build_active_preview_skin_indices(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names) {
    std::vector<std::size_t> skin_indices;
    if (const auto default_skin_index = skeleton.default_skin_index()) {
        skin_indices.push_back(*default_skin_index);
    }

    for (const std::string& skin_name : preview_skin_names) {
        const auto skin_index = skeleton.find_skin_index(skin_name);
        if (!skin_index.has_value()) {
            continue;
        }
        if (std::find(skin_indices.begin(), skin_indices.end(), *skin_index) == skin_indices.end()) {
            skin_indices.push_back(*skin_index);
        }
    }

    return skin_indices;
}

std::optional<PreviewAttachmentSelection> resolve_skin_preview_attachment(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names,
    std::size_t slot_index) {
    if (slot_index >= skeleton.slots().size()) {
        return std::nullopt;
    }

    const auto& slot = skeleton.slots()[slot_index];
    std::string attachment_name = slot.setup_attachment;
    std::optional<std::size_t> skin_index;
    if (!attachment_name.empty()) {
        skeleton.find_attachment_source(slot_index, attachment_name, &skin_index);
    }

    for (const std::size_t active_skin_index :
         build_active_preview_skin_indices(skeleton, preview_skin_names)) {
        const auto* attachment = skeleton.find_attachment(active_skin_index, slot_index);
        if (attachment == nullptr) {
            continue;
        }

        attachment_name = attachment->name;
        skin_index = active_skin_index;
    }

    if (attachment_name.empty()) {
        return std::nullopt;
    }

    return PreviewAttachmentSelection{slot_index, skin_index, attachment_name};
}

bool attachment_matches_selection(
    const PreviewAttachmentSelection& selection,
    const SlotAttachmentReference& reference) {
    return selection.slot_index == reference.slot_index &&
        selection.attachment_name == reference.attachment->name &&
        selection.skin_index == reference.skin_index;
}


std::vector<std::vector<std::size_t>> build_bone_slots(
    const marrow::runtime::SkeletonData& skeleton) {
    std::vector<std::vector<std::size_t>> bone_slots(skeleton.bones().size());
    for (std::size_t slot_index = 0; slot_index < skeleton.slots().size(); ++slot_index) {
        const std::size_t bone_index = skeleton.slots()[slot_index].bone_index;
        if (bone_index < bone_slots.size()) {
            bone_slots[bone_index].push_back(slot_index);
        }
    }
    return bone_slots;
}

Icon icon_for_attachment_kind(marrow::runtime::AttachmentKind kind) {
    switch (kind) {
    case marrow::runtime::AttachmentKind::Region:      return Icon::AttRegion;
    case marrow::runtime::AttachmentKind::Mesh:        return Icon::AttMesh;
    case marrow::runtime::AttachmentKind::LinkedMesh:  return Icon::AttLinked;
    case marrow::runtime::AttachmentKind::Point:       return Icon::AttPoint;
    case marrow::runtime::AttachmentKind::BoundingBox: return Icon::AttBbox;
    case marrow::runtime::AttachmentKind::Clipping:    return Icon::AttClip;
    case marrow::runtime::AttachmentKind::Path:        return Icon::AttPath;
    }
    return Icon::AttRegion;
}

struct PendingHierarchyClick {
    marrow::editor::SelectionItem item;
    HierarchySelectionModifiers modifiers;
};

struct HierarchyFrameRows {
    std::vector<marrow::editor::SelectionItem> visible_items;
    std::optional<PendingHierarchyClick> pending_click;
};

void record_hierarchy_row(
    HierarchyFrameRows* frame_rows,
    marrow::editor::SelectionItem item,
    bool clicked) {
    frame_rows->visible_items.push_back(item);
    if (!clicked) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    frame_rows->pending_click = PendingHierarchyClick{
        std::move(item),
        HierarchySelectionModifiers{
            hierarchy_command_modifier(
                io.ConfigMacOSXBehaviors,
                io.KeyCtrl,
                io.KeySuper),
            io.KeyShift}};
}

void draw_slot_hierarchy_node(
    ShellState* state,
    const marrow::runtime::SkeletonData& skeleton,
    HierarchyFrameRows* frame_rows,
    std::size_t slot_index) {
    const auto& slot = skeleton.slots()[slot_index];
    const std::vector<SlotAttachmentReference>& attachments =
        cached_slot_attachments(state, slot_index);
    const marrow::editor::SelectionItem slot_item =
        marrow::editor::SlotSelection{slot.name};
    const HierarchyRowSelectionState slot_state =
        hierarchy_row_selection_state(*state, slot_item);
    const bool slot_selected = slot_state != HierarchyRowSelectionState::Unselected;
    const bool slot_active = slot_state == HierarchyRowSelectionState::Active;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (slot_selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (attachments.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    ImGui::PushID(slot.name.c_str());
    bool row_clicked = false;
    const bool open = icon_tree_node(
        state->icons,
        "##slot",
        Icon::NodeSlot,
        slot.name.c_str(),
        flags,
        &row_clicked,
        slot_active);
    record_hierarchy_row(frame_rows, slot_item, row_clicked);

    if (open && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
        for (const SlotAttachmentReference& ref : attachments) {
            if (ref.attachment == nullptr || !ref.skin_index.has_value() ||
                *ref.skin_index >= skeleton.skins().size()) {
                continue;
            }
            const auto& skin = skeleton.skins()[*ref.skin_index];
            const marrow::editor::SelectionItem attachment_item =
                marrow::editor::AttachmentSelection{
                    slot.name,
                    skin.name,
                    ref.attachment->name};
            const HierarchyRowSelectionState attachment_state =
                hierarchy_row_selection_state(*state, attachment_item);
            const bool attach_selected =
                attachment_state != HierarchyRowSelectionState::Unselected;
            const bool attach_active =
                attachment_state == HierarchyRowSelectionState::Active;
            ImGuiTreeNodeFlags leaf_flags = ImGuiTreeNodeFlags_Leaf |
                                            ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                            ImGuiTreeNodeFlags_SpanAvailWidth;
            if (attach_selected) {
                leaf_flags |= ImGuiTreeNodeFlags_Selected;
            }
            ImGui::PushID(skin.name.c_str());
            ImGui::PushID(ref.attachment->name.c_str());
            bool leaf_clicked = false;
            icon_tree_node(
                state->icons,
                "##att",
                icon_for_attachment_kind(ref.attachment->kind),
                ref.attachment->name.c_str(),
                leaf_flags,
                &leaf_clicked,
                attach_active);
            record_hierarchy_row(frame_rows, attachment_item, leaf_clicked);
            ImGui::PopID();
            ImGui::PopID();
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void draw_hierarchy_node(
    ShellState* state,
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::vector<std::size_t>>& children,
    const std::vector<std::vector<std::size_t>>& bone_slots,
    const std::vector<std::size_t>& active_path,
    HierarchyFrameRows* frame_rows,
    std::size_t bone_index) {
    namespace t = marrow::editor::shell::theme;
    const auto& bone = skeleton.bones()[bone_index];
    const marrow::editor::SelectionItem bone_item =
        marrow::editor::BoneSelection{bone.name};
    const HierarchyRowSelectionState bone_state =
        hierarchy_row_selection_state(*state, bone_item);
    const bool selected = bone_state != HierarchyRowSelectionState::Unselected;
    const bool active = bone_state == HierarchyRowSelectionState::Active;
    // On the active path = this bone is the selected one or an ancestor of it.
    const bool on_path =
        std::find(active_path.begin(), active_path.end(), bone_index) !=
        active_path.end();
    const bool has_child_bones = !children[bone_index].empty();
    const bool has_slots = !bone_slots[bone_index].empty();
    const bool has_children_rows = has_child_bones || has_slots;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (!has_children_rows) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    } else if (has_child_bones) {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    ImGui::PushID(bone.name.c_str());
    const bool inactive =
        state->preview_skeleton && !state->preview_skeleton->is_bone_active(bone_index);
    const std::string display_name =
        bone.name + (inactive ? " (inactive)" : "");

    // Active-path nodes (ancestors of the selection) read brighter so the
    // trail from root to the selected bone is legible at a glance.
    const bool tint_label = on_path && !active;
    if (tint_label) {
        ImGui::PushStyleColor(ImGuiCol_Text, t::kPrimaryDim);
    }
    const ImVec2 node_origin = ImGui::GetCursorScreenPos();
    bool row_clicked = false;
    const bool open = icon_tree_node(
        state->icons,
        "##bone",
        Icon::NodeBone,
        display_name.c_str(),
        flags,
        &row_clicked,
        active);
    if (tint_label) {
        ImGui::PopStyleColor();
    }
    record_hierarchy_row(frame_rows, bone_item, row_clicked);

    if (open && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
        // Depth-rail: one continuous vertical guide per level, no branch
        // ticks. Coloured to the active path when the selection lives in
        // this subtree.
        const float rail_x =
            node_origin.x + ImGui::GetStyle().IndentSpacing * 0.5f;
        const float rail_top = ImGui::GetCursorScreenPos().y;
        for (const std::size_t child_index : children[bone_index]) {
            draw_hierarchy_node(
                state, skeleton, children, bone_slots, active_path,
                frame_rows,
                child_index);
        }
        for (const std::size_t slot_index : bone_slots[bone_index]) {
            draw_slot_hierarchy_node(state, skeleton, frame_rows, slot_index);
        }
        const float rail_bottom = ImGui::GetCursorScreenPos().y;
        ImGui::GetWindowDrawList()->AddLine(
            ImVec2(rail_x, rail_top),
            ImVec2(rail_x, rail_bottom),
            t::u32(on_path ? t::kPrimaryDim : t::kOutlineFaint),
            1.0f);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void draw_hierarchy_window(ShellState* state) {
    // A collapsed window or hidden dock tab renders no rows; reconciling the
    // anchor against that degenerate list would clear it (MAR-159 resets the
    // anchor only for filter/tree-collapse visibility loss).
    if (!ImGui::Begin(kHierarchyWindowTitle)) {
        ImGui::End();
        return;
    }
    widgets::panel_head(state->icons, Icon::NodeBone, "Hierarchy");

    if (!state->load_result) {
        ImGui::TextUnformatted("Load a valid project to inspect skeleton bones.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const ResolvedSelection resolved = resolve_shell_selection(*state);
    const auto children = build_bone_children(skeleton);
    const auto bone_slots = build_bone_slots(skeleton);

    // Walk parent links from the selection up to the root: every bone on
    // this chain is rendered as part of the active path.
    std::vector<std::size_t> active_path;
    if (resolved.context_bone_index.has_value()) {
        std::optional<std::size_t> walk = resolved.context_bone_index;
        while (walk.has_value() && *walk < skeleton.bones().size()) {
            active_path.push_back(*walk);
            walk = skeleton.bones()[*walk].parent_index;
        }
    }

    // Search input — ghost style (transparent frame, primary focus line).
    widgets::ghost_input_push();
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint(
        "##hierarchy_search",
        "Search bones, slots…",
        state->hierarchy_filter.data(),
        state->hierarchy_filter.size());
    widgets::ghost_input_pop();

    ImGui::TextDisabled("Bones: %zu · Slots: %zu",
        skeleton.bones().size(), skeleton.slots().size());
    ImGui::Separator();

    const std::string_view filter_sv(state->hierarchy_filter.data());
    const bool has_filter = !filter_sv.empty();
    HierarchyFrameRows frame_rows;

    if (has_filter) {
        // Flat list of matches across bones + slots.
        auto contains_case_insensitive = [](std::string_view haystack,
                                            std::string_view needle) {
            if (needle.empty()) return true;
            for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
                bool match = true;
                for (std::size_t j = 0; j < needle.size(); ++j) {
                    const char a = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(haystack[i + j])));
                    const char b = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(needle[j])));
                    if (a != b) { match = false; break; }
                }
                if (match) return true;
            }
            return false;
        };
        int matches = 0;
        for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
            const auto& bone = skeleton.bones()[bone_index];
            if (!contains_case_insensitive(bone.name, filter_sv)) continue;
            const marrow::editor::SelectionItem item =
                marrow::editor::BoneSelection{bone.name};
            const HierarchyRowSelectionState row_state =
                hierarchy_row_selection_state(*state, item);
            const bool selected = row_state != HierarchyRowSelectionState::Unselected;
            const bool active = row_state == HierarchyRowSelectionState::Active;
            ImGui::PushID(static_cast<int>(bone_index));
            const bool clicked = icon_selectable(
                state->icons,
                Icon::NodeBone,
                bone.name.c_str(),
                selected,
                active);
            record_hierarchy_row(&frame_rows, item, clicked);
            ImGui::PopID();
            ++matches;
        }
        for (std::size_t slot_index = 0; slot_index < skeleton.slots().size(); ++slot_index) {
            const auto& slot = skeleton.slots()[slot_index];
            if (!contains_case_insensitive(slot.name, filter_sv)) continue;
            const marrow::editor::SelectionItem item =
                marrow::editor::SlotSelection{slot.name};
            const HierarchyRowSelectionState row_state =
                hierarchy_row_selection_state(*state, item);
            const bool selected = row_state != HierarchyRowSelectionState::Unselected;
            const bool active = row_state == HierarchyRowSelectionState::Active;
            ImGui::PushID(static_cast<int>(10000 + slot_index));
            const bool clicked = icon_selectable(
                state->icons,
                Icon::NodeSlot,
                slot.name.c_str(),
                selected,
                active);
            record_hierarchy_row(&frame_rows, item, clicked);
            ImGui::PopID();
            ++matches;
        }
        if (matches == 0) {
            ImGui::TextDisabled("No matches");
        }
    } else {
        for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
            if (!skeleton.bones()[bone_index].parent_index.has_value()) {
                draw_hierarchy_node(
                    state, skeleton, children, bone_slots, active_path,
                    &frame_rows,
                    bone_index);
            }
        }
    }

    (void)reconcile_hierarchy_anchor_visibility(state, frame_rows.visible_items);
    if (frame_rows.pending_click.has_value()) {
        (void)apply_hierarchy_selection_gesture(
            state,
            frame_rows.visible_items,
            frame_rows.pending_click->item,
            frame_rows.pending_click->modifiers,
            true);
    }

    ImGui::End();
}


} // namespace marrow::editor::shell
