#include "shell_derived_cache.hpp"

#include <algorithm>
#include <utility>

#include "shell_timeline.hpp"

namespace marrow::editor::shell {
namespace {

const std::vector<TimelineTrackRow> kEmptyTimelineTracks;
const std::vector<SlotAttachmentReference> kEmptySlotAttachments;
const std::vector<std::string> kEmptyAttachmentNames;

void rebuild_slot_cache(ShellState* state) {
    SlotDerivedCache rebuilt;
    rebuilt.runtime_revision = state->session.runtime_revision();
    rebuilt.runtime = state->load_result.skeleton_data;
    rebuilt.generation = state->slot_derived_cache.generation + 1U;
    rebuilt.valid = true;

    if (rebuilt.runtime != nullptr) {
        rebuilt.slots.resize(rebuilt.runtime->slots().size());
        for (std::size_t skin_index = 0U;
             skin_index < rebuilt.runtime->skins().size();
             ++skin_index) {
            const auto& skin = rebuilt.runtime->skins()[skin_index];
            for (const auto& slot_attachment : skin.slot_attachments) {
                if (slot_attachment.slot_index >= rebuilt.slots.size()) {
                    continue;
                }
                auto& slot = rebuilt.slots[slot_attachment.slot_index];
                slot.authored_attachments.push_back(SlotAttachmentReference{
                    slot_attachment.slot_index,
                    skin_index,
                    &slot_attachment.attachment});
                slot.timeline_attachment_names.push_back(
                    slot_attachment.attachment.name);
            }
        }
        for (auto& slot : rebuilt.slots) {
            std::sort(
                slot.timeline_attachment_names.begin(),
                slot.timeline_attachment_names.end());
            slot.timeline_attachment_names.erase(
                std::unique(
                    slot.timeline_attachment_names.begin(),
                    slot.timeline_attachment_names.end()),
                slot.timeline_attachment_names.end());
        }
    }

    state->slot_derived_cache = std::move(rebuilt);
}

bool slot_cache_matches(const ShellState& state) {
    return state.slot_derived_cache.valid &&
        state.slot_derived_cache.runtime_revision == state.session.runtime_revision() &&
        state.slot_derived_cache.runtime.get() == state.load_result.skeleton_data.get();
}

} // namespace

const std::vector<TimelineTrackRow>& cached_timeline_tracks(ShellState* state) {
    if (state == nullptr) {
        return kEmptyTimelineTracks;
    }

    const auto* skeleton = state->load_result.skeleton_data.get();
    auto& cache = state->timeline_track_cache;
    const std::uint64_t runtime_revision = state->session.runtime_revision();
    if (!cache.valid || cache.runtime_revision != runtime_revision ||
        cache.skeleton_identity != skeleton ||
        cache.animation_name != state->selected_animation_name) {
        cache.runtime_revision = runtime_revision;
        cache.skeleton_identity = skeleton;
        cache.animation_name = state->selected_animation_name;
        cache.tracks.clear();
        if (skeleton != nullptr && !cache.animation_name.empty()) {
            if (const auto* animation = skeleton->find_animation(cache.animation_name)) {
                cache.tracks = build_timeline_tracks(*skeleton, *animation);
            }
        }
        ++cache.generation;
        cache.valid = true;
    }
    return cache.tracks;
}

const std::vector<SlotAttachmentReference>& cached_slot_attachments(
    ShellState* state,
    std::size_t slot_index) {
    if (state == nullptr) {
        return kEmptySlotAttachments;
    }
    if (!slot_cache_matches(*state)) {
        rebuild_slot_cache(state);
    }
    return slot_index < state->slot_derived_cache.slots.size()
        ? state->slot_derived_cache.slots[slot_index].authored_attachments
        : kEmptySlotAttachments;
}

const std::vector<std::string>& cached_timeline_attachment_names(
    ShellState* state,
    std::size_t slot_index) {
    if (state == nullptr) {
        return kEmptyAttachmentNames;
    }
    if (!slot_cache_matches(*state)) {
        rebuild_slot_cache(state);
    }
    return slot_index < state->slot_derived_cache.slots.size()
        ? state->slot_derived_cache.slots[slot_index].timeline_attachment_names
        : kEmptyAttachmentNames;
}

} // namespace marrow::editor::shell
