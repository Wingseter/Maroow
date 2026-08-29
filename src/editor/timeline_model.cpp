#include "timeline_model.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor::timeline_model {

template <typename Timeline>
void append_timeline_key_times(
    const std::vector<Timeline>& timelines,
    std::vector<double>* key_times) {
    if (key_times == nullptr) {
        return;
    }

    for (const Timeline& timeline : timelines) {
        for (const auto& keyframe : timeline.keyframes) {
            key_times->push_back(static_cast<double>(keyframe.time));
        }
    }
}

std::vector<double> collect_animation_key_times(const marrow::runtime::AnimationData& animation) {
    std::vector<double> key_times;
    append_timeline_key_times(animation.bone_rotate_timelines, &key_times);
    append_timeline_key_times(animation.bone_inherit_timelines, &key_times);
    append_timeline_key_times(animation.bone_translate_timelines, &key_times);
    append_timeline_key_times(animation.bone_scale_timelines, &key_times);
    append_timeline_key_times(animation.bone_shear_timelines, &key_times);
    append_timeline_key_times(animation.slot_attachment_timelines, &key_times);
    append_timeline_key_times(animation.slot_color_timelines, &key_times);
    append_timeline_key_times(animation.mesh_deform_timelines, &key_times);
    if (animation.draw_order_timeline_data.has_value()) {
        for (const auto& keyframe : animation.draw_order_timeline_data->keyframes) {
            key_times.push_back(static_cast<double>(keyframe.time));
        }
    }
    if (animation.event_timeline_data.has_value()) {
        for (const auto& keyframe : animation.event_timeline_data->keyframes) {
            key_times.push_back(static_cast<double>(keyframe.time));
        }
    }

    std::sort(key_times.begin(), key_times.end());
    key_times.erase(
        std::unique(
            key_times.begin(),
            key_times.end(),
            [](double lhs, double rhs) { return std::abs(lhs - rhs) <= 1e-6; }),
        key_times.end());
    return key_times;
}


template <typename Keyframe>
std::vector<double> collect_key_times(const std::vector<Keyframe>& keyframes) {
    std::vector<double> key_times;
    key_times.reserve(keyframes.size());
    for (const Keyframe& keyframe : keyframes) {
        key_times.push_back(static_cast<double>(keyframe.time));
    }
    return key_times;
}

std::vector<TrackRow> build_tracks(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AnimationData& animation) {
    std::vector<TrackRow> tracks;
    tracks.reserve(
        animation.bone_rotate_timelines.size() +
        animation.bone_inherit_timelines.size() +
        animation.bone_translate_timelines.size() +
        animation.bone_scale_timelines.size() +
        animation.bone_shear_timelines.size() +
        animation.slot_attachment_timelines.size() +
        animation.slot_color_timelines.size() +
        animation.mesh_deform_timelines.size() +
        (animation.draw_order_timeline_data.has_value() ? 1U : 0U) +
        (animation.event_timeline_data.has_value() ? 1U : 0U));

    const auto add_bone_track = [&](std::string_view suffix,
                                    std::size_t bone_index,
                                    const auto& keyframes,
                                    std::optional<marrow::editor::TransformTimelineChannel> transform_channel,
                                    TimelineTrackKind kind) {
        if (bone_index >= skeleton.bones().size() || keyframes.empty()) {
            return;
        }

        tracks.push_back(TrackRow{
            "bone:" + std::to_string(bone_index) + ":" + std::string(suffix),
            "Bone / " + skeleton.bones()[bone_index].name + " / " + std::string(suffix),
            animation.name,
            collect_key_times(keyframes),
            bone_index,
            std::nullopt,
            transform_channel,
            std::nullopt,
            kind});
    };
    const auto add_slot_track = [&](std::string_view suffix,
                                    std::size_t slot_index,
                                    const auto& keyframes,
                                    TimelineTrackKind kind) {
        if (slot_index >= skeleton.slots().size() || keyframes.empty()) {
            return;
        }

        tracks.push_back(TrackRow{
            "slot:" + std::to_string(slot_index) + ":" + std::string(suffix),
            "Slot / " + skeleton.slots()[slot_index].name + " / " + std::string(suffix),
            animation.name,
            collect_key_times(keyframes),
            std::nullopt,
            slot_index,
            std::nullopt,
            std::nullopt,
            kind});
    };

    for (const auto& timeline : animation.bone_rotate_timelines) {
        add_bone_track(
            "Rotate",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Rotate,
            TimelineTrackKind::Rotate);
    }
    for (const auto& timeline : animation.bone_translate_timelines) {
        add_bone_track(
            "Translate",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Translate,
            TimelineTrackKind::Translate);
    }
    for (const auto& timeline : animation.bone_scale_timelines) {
        add_bone_track(
            "Scale",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Scale,
            TimelineTrackKind::Scale);
    }
    for (const auto& timeline : animation.bone_shear_timelines) {
        add_bone_track(
            "Shear",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Shear,
            TimelineTrackKind::Shear);
    }
    for (const auto& timeline : animation.bone_inherit_timelines) {
        add_bone_track(
            "Inherit",
            timeline.bone_index,
            timeline.keyframes,
            std::nullopt,
            TimelineTrackKind::Inherit);
    }
    for (const auto& timeline : animation.slot_attachment_timelines) {
        add_slot_track(
            "Attachment",
            timeline.slot_index,
            timeline.keyframes,
            TimelineTrackKind::SlotAttachment);
    }
    for (const auto& timeline : animation.slot_color_timelines) {
        add_slot_track(
            "Color",
            timeline.slot_index,
            timeline.keyframes,
            TimelineTrackKind::SlotColor);
    }
    for (const auto& timeline : animation.mesh_deform_timelines) {
        if (timeline.slot_index >= skeleton.slots().size() || timeline.keyframes.empty()) {
            continue;
        }

        tracks.push_back(TrackRow{
            "slot:" + std::to_string(timeline.slot_index) + ":deform:" + timeline.attachment_name,
            "Slot / " + skeleton.slots()[timeline.slot_index].name +
                " / Deform / " + timeline.attachment_name,
            animation.name,
            collect_key_times(timeline.keyframes),
            std::nullopt,
            timeline.slot_index,
            std::nullopt,
            timeline.attachment_name,
            TimelineTrackKind::Deform});
    }

    if (animation.draw_order_timeline_data.has_value() &&
        !animation.draw_order_timeline_data->keyframes.empty()) {
        tracks.push_back(TrackRow{
            "global:draw-order",
            "Global / Draw Order",
            animation.name,
            collect_key_times(animation.draw_order_timeline_data->keyframes),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            TimelineTrackKind::DrawOrder});
    }

    if (animation.event_timeline_data.has_value() &&
        !animation.event_timeline_data->keyframes.empty()) {
        tracks.push_back(TrackRow{
            "global:events",
            "Global / Events",
            animation.name,
            collect_key_times(animation.event_timeline_data->keyframes),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt,
            TimelineTrackKind::Event});
    }

    return tracks;
}

const TrackRow* find_track(
    const std::vector<TrackRow>& tracks,
    std::string_view track_id) {
    const auto iterator = std::find_if(
        tracks.begin(),
        tracks.end(),
        [&](const TrackRow& track) { return track.id == track_id; });
    return iterator == tracks.end() ? nullptr : &(*iterator);
}

bool track_is_editable(const TrackRow& track) {
    return track.transform_channel.has_value() ||
        track.deform_attachment_name.has_value() ||
        (track.slot_index.has_value() &&
         (track.id.find(":Color") != std::string::npos ||
          track.id.find(":Attachment") != std::string::npos)) ||
        track.id == "global:draw-order" ||
        track.id == "global:events";
}


std::int64_t time_identity(double time_seconds) {
    return static_cast<std::int64_t>(
        std::llround(time_seconds * 1'000'000.0));
}

KeyRef key_ref(const TrackRow& track, std::size_t key_index_value) {
    KeyRef result;
    result.track_id = track.id;
    if (key_index_value >= track.key_times.size()) {
        return result;
    }
    result.time_microseconds = time_identity(track.key_times[key_index_value]);
    result.same_time_count = 0U;
    for (std::size_t index = 0U; index < track.key_times.size(); ++index) {
        if (time_identity(track.key_times[index]) != result.time_microseconds) {
            continue;
        }
        if (index < key_index_value) {
            ++result.same_time_ordinal;
        }
        ++result.same_time_count;
    }
    return result;
}

std::optional<std::size_t> key_index(
    const TrackRow& track,
    const KeyRef& key) {
    if (key.track_id != track.id || key.same_time_count == 0U) {
        return std::nullopt;
    }
    std::size_t same_time_count = 0U;
    std::optional<std::size_t> resolved;
    for (std::size_t index = 0U; index < track.key_times.size(); ++index) {
        if (time_identity(track.key_times[index]) != key.time_microseconds) {
            continue;
        }
        if (same_time_count == key.same_time_ordinal) {
            resolved = index;
        }
        ++same_time_count;
    }
    if (same_time_count != key.same_time_count) {
        return std::nullopt;
    }
    return resolved;
}

void apply_key_activation(
    std::vector<KeyRef>* selection,
    std::optional<KeyRef>* active_key,
    const KeyRef& clicked_key,
    bool additive) {
    if (selection == nullptr || active_key == nullptr) {
        return;
    }
    const auto found = std::find(selection->begin(), selection->end(), clicked_key);
    if (!additive) {
        if (found == selection->end()) {
            *selection = {clicked_key};
        }
        *active_key = clicked_key;
        return;
    }
    if (found == selection->end()) {
        selection->push_back(clicked_key);
        *active_key = clicked_key;
        return;
    }
    const bool removed_active = *active_key == std::optional<KeyRef>(clicked_key);
    selection->erase(found);
    if (removed_active || !active_key->has_value() ||
        std::find(selection->begin(), selection->end(), **active_key) == selection->end()) {
        *active_key = selection->empty()
            ? std::nullopt
            : std::optional<KeyRef>(selection->back());
    }
}

void reconcile_selection(
    std::vector<KeyRef>* selection,
    const std::vector<TrackRow>& tracks) {
    if (selection == nullptr) {
        return;
    }
    selection->erase(
        std::remove_if(
            selection->begin(),
            selection->end(),
            [&](const KeyRef& key) {
                const TrackRow* track = find_track(tracks, key.track_id);
                return track == nullptr || !key_index(*track, key).has_value();
            }),
        selection->end());
    std::vector<KeyRef> unique;
    unique.reserve(selection->size());
    for (const KeyRef& key : *selection) {
        if (std::find(unique.begin(), unique.end(), key) == unique.end()) {
            unique.push_back(key);
        }
    }
    *selection = std::move(unique);
}

void reconcile_selection(
    std::vector<KeyRef>* selection,
    std::optional<KeyRef>* active_key,
    const std::vector<TrackRow>& tracks) {
    if (selection == nullptr || active_key == nullptr) {
        return;
    }
    reconcile_selection(selection, tracks);
    if (!active_key->has_value() ||
        std::find(selection->begin(), selection->end(), **active_key) == selection->end()) {
        *active_key = selection->empty()
            ? std::nullopt
            : std::optional<KeyRef>(selection->back());
    }
}

std::vector<std::size_t> selected_indices(
    const std::vector<KeyRef>& selection,
    const TrackRow& track) {
    std::vector<std::size_t> indices;
    for (const KeyRef& key : selection) {
        if (key.track_id == track.id) {
            if (const auto index = key_index(track, key)) {
                indices.push_back(*index);
            }
        }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

std::size_t clipboard_track_count(const Clipboard& clipboard) {
    return clipboard.project_fragment.transform_timeline_edits.size() +
        clipboard.project_fragment.mesh_deform_timeline_edits.size() +
        clipboard.project_fragment.draw_order_timeline_edits.size() +
        clipboard.project_fragment.event_timeline_edits.size() +
        clipboard.project_fragment.slot_color_timeline_edits.size() +
        clipboard.project_fragment.slot_attachment_timeline_edits.size();
}

std::optional<double> clipboard_time_shift(
    const Clipboard& clipboard,
    std::string_view animation_name,
    double playhead_time) {
    if (!clipboard.has_data || clipboard.animation_name != animation_name ||
        !std::isfinite(clipboard.earliest_time) ||
        !std::isfinite(playhead_time)) {
        return std::nullopt;
    }
    const double shift = playhead_time - clipboard.earliest_time;
    return std::isfinite(shift) ? std::optional<double>(shift) : std::nullopt;
}

std::optional<double> snap_delta_to_frames(
    double earliest_time,
    double requested_delta,
    double frames_per_second) {
    if (!std::isfinite(earliest_time) || !std::isfinite(requested_delta) ||
        !std::isfinite(frames_per_second) || frames_per_second <= 0.0) {
        return std::nullopt;
    }
    const double frame_seconds = 1.0 / frames_per_second;
    return std::round((earliest_time + requested_delta) / frame_seconds) *
            frame_seconds -
        earliest_time;
}

std::optional<double> incremental_retime_delta(
    double requested_delta,
    double applied_delta) {
    if (!std::isfinite(requested_delta) || !std::isfinite(applied_delta)) {
        return std::nullopt;
    }
    const double delta = requested_delta - applied_delta;
    return std::isfinite(delta) ? std::optional<double>(delta) : std::nullopt;
}

CompletionDecision completion_decision(
    bool commit_requested,
    bool changed) {
    if (commit_requested && changed) {
        return {CompletionAction::Commit, false, 1U};
    }
    return {CompletionAction::Cancel, !commit_requested, 0U};
}

std::optional<double> adjacent_key_time(
    const std::vector<TrackRow>& tracks,
    double current_time,
    bool forward) {
    std::optional<double> best_time;

    for (const TrackRow& track : tracks) {
        for (const double key_time : track.key_times) {
            if (forward) {
                if (key_time <= current_time + 1e-6) {
                    continue;
                }
                if (!best_time.has_value() || key_time < *best_time) {
                    best_time = key_time;
                }
            } else {
                if (key_time >= current_time - 1e-6) {
                    continue;
                }
                if (!best_time.has_value() || key_time > *best_time) {
                    best_time = key_time;
                }
            }
        }
    }

    return best_time;
}

std::optional<double> nearest_key_time(
    const TrackRow& track,
    double target_time,
    double threshold_time) {
    std::optional<double> best_time;
    double best_distance = threshold_time;

    for (const double key_time : track.key_times) {
        const double distance = std::abs(key_time - target_time);
        if (distance > best_distance) {
            continue;
        }

        best_distance = distance;
        best_time = key_time;
    }

    return best_time;
}


} // namespace marrow::editor::timeline_model
