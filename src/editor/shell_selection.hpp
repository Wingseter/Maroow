#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell {

struct ResolvedSelection {
    std::optional<std::size_t> active_bone_index;
    std::optional<std::size_t> active_slot_index;
    std::optional<std::size_t> context_bone_index;
    std::optional<PreviewAttachmentSelection> active_attachment;
    std::optional<PreviewAttachmentSelection> attachment_context;
    std::optional<marrow::editor::ConstraintSelection> active_constraint;
};

struct HierarchySelectionModifiers {
    bool command{false};
    bool shift{false};
};

enum class HierarchyRowSelectionState {
    Unselected,
    Selected,
    Active,
};

ResolvedSelection resolve_shell_selection(const ShellState& state);
bool hierarchy_command_modifier(
    bool config_macosx_behaviors,
    bool key_ctrl,
    bool key_super) noexcept;
bool apply_hierarchy_selection_gesture(
    ShellState* state,
    const std::vector<marrow::editor::SelectionItem>& visible_items,
    const marrow::editor::SelectionItem& clicked_item,
    HierarchySelectionModifiers modifiers,
    bool update_status_message = true);
bool apply_viewport_point_selection_gesture(
    ShellState* state,
    const marrow::editor::SelectionItem& clicked_item,
    bool command_modifier,
    bool update_status_message = true);
bool apply_viewport_box_selection_gesture(
    ShellState* state,
    const std::vector<marrow::editor::SelectionItem>& ordered_bones,
    bool additive,
    bool update_status_message = true);
bool reconcile_hierarchy_anchor_visibility(
    ShellState* state,
    const std::vector<marrow::editor::SelectionItem>& visible_items);
bool reconcile_hierarchy_anchor_to_runtime(
    ShellState* state,
    const marrow::runtime::SkeletonData& skeleton);
HierarchyRowSelectionState hierarchy_row_selection_state(
    const ShellState& state,
    const marrow::editor::SelectionItem& item) noexcept;
std::string join_strings(const std::vector<std::string>& values);
void select_bone(
    ShellState* state,
    std::optional<std::size_t> bone_index,
    std::string_view source,
    bool update_status_message);
void select_slot(
    ShellState* state,
    std::optional<std::size_t> slot_index,
    std::string_view source,
    bool update_status_message);
void select_attachment(
    ShellState* state,
    std::optional<PreviewAttachmentSelection> selection,
    std::string_view source,
    bool update_status_message);
bool apply_preview_skin_selection(
    ShellState* state,
    std::string_view source,
    bool update_status_message);
bool set_preview_skin_enabled(
    ShellState* state,
    std::size_t skin_index,
    bool enabled,
    bool update_status_message,
    bool record_history = true);
bool apply_attachment_selection_to_preview_slot(
    ShellState* state,
    const PreviewAttachmentSelection& selection,
    std::string_view source,
    bool update_status_message,
    bool record_history = true);
bool reset_preview_slot_to_skin_selection(
    ShellState* state,
    std::size_t slot_index,
    std::string_view source,
    bool update_status_message,
    bool record_history = true);

std::optional<std::string_view> default_skin_name(
    const marrow::runtime::SkeletonData& skeleton);
bool is_default_skin_index(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t skin_index);
std::string source_skin_name(
    const marrow::runtime::SkeletonData& skeleton,
    std::optional<std::size_t> skin_index);
std::string preview_skin_summary(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names);
std::vector<SlotAttachmentReference> collect_slot_attachments(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index);
std::optional<SlotAttachmentReference> resolve_attachment_reference(
    const marrow::runtime::SkeletonData& skeleton,
    const PreviewAttachmentSelection& selection);
std::optional<PreviewAttachmentSelection> current_attachment_selection(
    const ShellState& state,
    std::size_t slot_index);
std::vector<std::size_t> build_active_preview_skin_indices(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names);
std::optional<PreviewAttachmentSelection> resolve_skin_preview_attachment(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names,
    std::size_t slot_index);
bool attachment_matches_selection(
    const PreviewAttachmentSelection& selection,
    const SlotAttachmentReference& reference);
std::vector<std::vector<std::size_t>> build_bone_children(
    const marrow::runtime::SkeletonData& skeleton);

void draw_hierarchy_window(ShellState* state);

} // namespace marrow::editor::shell
