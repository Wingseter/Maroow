#include "shell_selection.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_theme.hpp"
#include "shell_preview.hpp"
#include "shell_widgets.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;
using marrow::editor::IconRegistry;

std::optional<std::size_t> selected_slot_index(const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    if (const auto* slot = state.selection.active_slot()) {
        return skeleton.find_slot_index(slot->slot_name);
    }
    if (const auto* attachment = state.selection.active_attachment()) {
        return skeleton.find_slot_index(attachment->slot_name);
    }
    return std::nullopt;
}

std::optional<std::size_t> selected_bone_index(const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    if (const auto* bone = state.selection.active_bone()) {
        return skeleton.find_bone_index(bone->bone_name);
    }
    const auto slot_index = selected_slot_index(state);
    if (!slot_index.has_value() || *slot_index >= skeleton.slots().size()) {
        return std::nullopt;
    }
    return skeleton.slots()[*slot_index].bone_index;
}

std::optional<PreviewAttachmentSelection> selected_attachment(const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    if (const auto* attachment = state.selection.active_attachment()) {
        const auto slot_index = skeleton.find_slot_index(attachment->slot_name);
        const auto skin_index = skeleton.find_skin_index(attachment->skin_name);
        if (!slot_index.has_value() || !skin_index.has_value() ||
            skeleton.find_attachment(
                *skin_index,
                *slot_index,
                attachment->attachment_name) == nullptr) {
            return std::nullopt;
        }
        return PreviewAttachmentSelection{
            *slot_index,
            *skin_index,
            attachment->attachment_name};
    }

    const auto slot_index = selected_slot_index(state);
    return slot_index.has_value()
        ? current_attachment_selection(state, *slot_index)
        : std::nullopt;
}

std::optional<marrow::editor::ConstraintSelection> selected_constraint(
    const ShellState& state) {
    const auto* selection = state.selection.active_constraint();
    return selection == nullptr
        ? std::nullopt
        : std::optional<marrow::editor::ConstraintSelection>(*selection);
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
    if (!bone_index.has_value() || !state->load_result ||
        *bone_index >= state->load_result.skeleton_data->bones().size()) {
        state->selection.clear();
        return;
    }

    const auto& bones = state->load_result.skeleton_data->bones();
    state->selection.replace(marrow::editor::BoneSelection{bones[*bone_index].name});
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

void sync_attachment_selection_for_slot(ShellState* state, std::size_t slot_index) {
    const auto* active_attachment = state->selection.active_attachment();
    if (active_attachment == nullptr) {
        return;
    }
    if (!state->load_result || slot_index >= state->load_result.skeleton_data->slots().size()) {
        state->selection.remap(*active_attachment, std::nullopt);
        return;
    }

    if (active_attachment->slot_name ==
            state->load_result.skeleton_data->slots()[slot_index].name &&
        selected_attachment(*state).has_value()) {
        return;
    }

    if (const auto current_selection = current_attachment_selection(*state, slot_index)) {
        select_attachment(state, current_selection, "", false);
        return;
    }

    select_attachment(
        state,
        first_attachment_selection_for_slot(*state->load_result.skeleton_data, slot_index),
        "",
        false);
}

void select_attachment(
    ShellState* state,
    std::optional<PreviewAttachmentSelection> selection,
    std::string_view source,
    bool update_status_message) {
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
    if (!slot_index.has_value() || !state->load_result ||
        *slot_index >= state->load_result.skeleton_data->slots().size()) {
        state->selection.clear();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    state->selection.replace(marrow::editor::SlotSelection{
        skeleton.slots()[*slot_index].name});

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

    if (selected_slot_index(*state).has_value()) {
        sync_attachment_selection_for_slot(state, *selected_slot_index(*state));
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

    if (selected_slot_index(*state).has_value()) {
        sync_attachment_selection_for_slot(state, *selected_slot_index(*state));
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
            skeleton.find_attachment(*selection.skin_index, selection.slot_index);
        if (attachment != nullptr && attachment->name == selection.attachment_name) {
            return SlotAttachmentReference{
                selection.slot_index,
                selection.skin_index,
                attachment};
        }
    }

    std::optional<std::size_t> source_skin_index = selection.skin_index;
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


std::optional<PreviewAttachmentSelection> first_attachment_selection_for_slot(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index) {
    const auto attachments = collect_slot_attachments(skeleton, slot_index);
    if (attachments.empty()) {
        return std::nullopt;
    }

    return PreviewAttachmentSelection{
        slot_index,
        attachments.front().skin_index,
        attachments.front().attachment->name};
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

void draw_slot_hierarchy_node(
    ShellState* state,
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index) {
    const auto& slot = skeleton.slots()[slot_index];
    const std::vector<SlotAttachmentReference> attachments =
        collect_slot_attachments(skeleton, slot_index);
    const auto* active_slot = state->selection.active_slot();
    const bool slot_selected =
        active_slot != nullptr && active_slot->slot_name == slot.name;

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth;
    if (slot_selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (attachments.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    ImGui::PushID(static_cast<int>(slot_index) ^ 0x40000000);
    bool row_clicked = false;
    const bool open = icon_tree_node(
        state->icons,
        "##slot",
        Icon::NodeSlot,
        slot.name.c_str(),
        flags,
        &row_clicked);
    if (row_clicked) {
        select_slot(state, std::optional<std::size_t>(slot_index), "Hierarchy", true);
    }

    if (open && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
        for (const SlotAttachmentReference& ref : attachments) {
            if (ref.attachment == nullptr) continue;
            const bool attach_selected =
                state->selection.active_attachment() != nullptr &&
                selected_attachment(*state).has_value() &&
                selected_attachment(*state)->slot_index == slot_index &&
                selected_attachment(*state)->skin_index == ref.skin_index &&
                selected_attachment(*state)->attachment_name == ref.attachment->name;
            ImGuiTreeNodeFlags leaf_flags = ImGuiTreeNodeFlags_Leaf |
                                            ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                            ImGuiTreeNodeFlags_SpanAvailWidth;
            if (attach_selected) {
                leaf_flags |= ImGuiTreeNodeFlags_Selected;
            }
            ImGui::PushID(ref.attachment->name.c_str());
            bool leaf_clicked = false;
            icon_tree_node(
                state->icons,
                "##att",
                icon_for_attachment_kind(ref.attachment->kind),
                ref.attachment->name.c_str(),
                leaf_flags,
                &leaf_clicked);
            if (leaf_clicked) {
                PreviewAttachmentSelection sel;
                sel.slot_index = slot_index;
                sel.skin_index = ref.skin_index;
                sel.attachment_name = ref.attachment->name;
                select_attachment(state, sel, "Hierarchy", true);
            }
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
    std::size_t bone_index) {
    namespace t = marrow::editor::shell::theme;
    const auto& bone = skeleton.bones()[bone_index];
    const bool selected =
        selected_bone_index(*state).has_value() && *selected_bone_index(*state) == bone_index;
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

    ImGui::PushID(static_cast<int>(bone_index));
    const bool inactive =
        state->preview_skeleton && !state->preview_skeleton->is_bone_active(bone_index);
    const std::string display_name =
        bone.name + (inactive ? " (inactive)" : "");

    // Active-path nodes (ancestors of the selection) read brighter so the
    // trail from root to the selected bone is legible at a glance.
    const bool tint_label = on_path && !selected;
    if (tint_label) {
        ImGui::PushStyleColor(ImGuiCol_Text, t::kPrimary);
    }
    const ImVec2 node_origin = ImGui::GetCursorScreenPos();
    bool row_clicked = false;
    const bool open = icon_tree_node(
        state->icons,
        "##bone",
        Icon::NodeBone,
        display_name.c_str(),
        flags,
        &row_clicked);
    if (tint_label) {
        ImGui::PopStyleColor();
    }
    if (row_clicked) {
        select_bone(state, bone_index, "Hierarchy", true);
    }

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
                child_index);
        }
        for (const std::size_t slot_index : bone_slots[bone_index]) {
            draw_slot_hierarchy_node(state, skeleton, slot_index);
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
    ImGui::Begin(kHierarchyWindowTitle);
    widgets::panel_head(state->icons, Icon::NodeBone, "Hierarchy");

    if (!state->load_result) {
        ImGui::TextUnformatted("Load a valid project to inspect skeleton bones.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const auto children = build_bone_children(skeleton);
    const auto bone_slots = build_bone_slots(skeleton);

    // Walk parent links from the selection up to the root: every bone on
    // this chain is rendered as part of the active path.
    std::vector<std::size_t> active_path;
    if (selected_bone_index(*state).has_value()) {
        std::optional<std::size_t> walk = selected_bone_index(*state);
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
            const bool selected =
                selected_bone_index(*state).has_value() &&
                *selected_bone_index(*state) == bone_index;
            ImGui::PushID(static_cast<int>(bone_index));
            if (icon_selectable(state->icons, Icon::NodeBone, bone.name.c_str(), selected)) {
                select_bone(state, bone_index, "Hierarchy", true);
            }
            ImGui::PopID();
            ++matches;
        }
        for (std::size_t slot_index = 0; slot_index < skeleton.slots().size(); ++slot_index) {
            const auto& slot = skeleton.slots()[slot_index];
            if (!contains_case_insensitive(slot.name, filter_sv)) continue;
            const bool selected =
                selected_slot_index(*state).has_value() &&
                *selected_slot_index(*state) == slot_index;
            ImGui::PushID(static_cast<int>(10000 + slot_index));
            if (icon_selectable(state->icons, Icon::NodeSlot, slot.name.c_str(), selected)) {
                select_slot(state, std::optional<std::size_t>(slot_index), "Hierarchy", true);
            }
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
                    bone_index);
            }
        }
    }

    ImGui::End();
}


} // namespace marrow::editor::shell
