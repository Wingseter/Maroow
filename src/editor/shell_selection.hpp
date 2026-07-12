#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell {

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
    std::optional<AttachmentSelection> selection,
    std::string_view source,
    bool update_status_message);
void sync_attachment_selection_for_slot(
    ShellState* state,
    std::size_t slot_index);
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
    const AttachmentSelection& selection,
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
    const AttachmentSelection& selection);
std::optional<AttachmentSelection> current_attachment_selection(
    const ShellState& state,
    std::size_t slot_index);
std::optional<AttachmentSelection> first_attachment_selection_for_slot(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index);
std::vector<std::size_t> build_active_preview_skin_indices(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names);
std::optional<AttachmentSelection> resolve_skin_preview_attachment(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names,
    std::size_t slot_index);
bool attachment_matches_selection(
    const AttachmentSelection& selection,
    const SlotAttachmentReference& reference);
std::vector<std::vector<std::size_t>> build_bone_children(
    const marrow::runtime::SkeletonData& skeleton);

void draw_hierarchy_window(ShellState* state);

} // namespace marrow::editor::shell
