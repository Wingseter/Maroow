#pragma once

#include <cstddef>
#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell {

const std::vector<TimelineTrackRow>& cached_timeline_tracks(ShellState* state);
const std::vector<SlotAttachmentReference>& cached_slot_attachments(
    ShellState* state,
    std::size_t slot_index);
const std::vector<std::string>& cached_timeline_attachment_names(
    ShellState* state,
    std::size_t slot_index);

} // namespace marrow::editor::shell
