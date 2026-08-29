#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/runtime/animation.hpp"

namespace marrow::editor::timeline_model {

constexpr double kKeyTimeEpsilon = 1e-6;
constexpr double kNonEventKeySpacing = 0.001;

enum class TimelineTrackKind : std::uint8_t {
    Unknown,
    Rotate,
    Translate,
    Scale,
    Shear,
    Inherit,
    SlotAttachment,
    SlotColor,
    Deform,
    DrawOrder,
    Event,
};

struct TrackRow {
    std::string id;
    std::string label;
    std::string animation_name;
    std::vector<double> key_times;
    std::optional<std::size_t> bone_index;
    std::optional<std::size_t> slot_index;
    std::optional<marrow::editor::TransformTimelineChannel> transform_channel;
    std::optional<std::string> deform_attachment_name;
    TimelineTrackKind kind{TimelineTrackKind::Unknown};
};

struct KeyRef {
    std::string track_id;
    std::int64_t time_microseconds{0};
    std::size_t same_time_ordinal{0U};
    std::size_t same_time_count{1U};

    friend bool operator==(const KeyRef& left, const KeyRef& right) {
        return left.track_id == right.track_id &&
            left.time_microseconds == right.time_microseconds &&
            left.same_time_ordinal == right.same_time_ordinal &&
            left.same_time_count == right.same_time_count;
    }
};

struct Clipboard {
    bool has_data{false};
    std::string animation_name;
    double earliest_time{0.0};
    marrow::editor::ProjectData project_fragment;
};

std::vector<double> collect_animation_key_times(
    const marrow::runtime::AnimationData& animation);
std::vector<TrackRow> build_tracks(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AnimationData& animation);
std::int64_t time_identity(double time_seconds);
KeyRef key_ref(const TrackRow& track, std::size_t key_index);
std::optional<std::size_t> key_index(
    const TrackRow& track,
    const KeyRef& key);
void apply_key_activation(
    std::vector<KeyRef>* selection,
    std::optional<KeyRef>* active_key,
    const KeyRef& clicked_key,
    bool additive);
void reconcile_selection(
    std::vector<KeyRef>* selection,
    const std::vector<TrackRow>& tracks);
void reconcile_selection(
    std::vector<KeyRef>* selection,
    std::optional<KeyRef>* active_key,
    const std::vector<TrackRow>& tracks);
const TrackRow* find_track(
    const std::vector<TrackRow>& tracks,
    std::string_view track_id);
bool track_is_editable(const TrackRow& track);
std::optional<double> adjacent_key_time(
    const std::vector<TrackRow>& tracks,
    double current_time,
    bool forward);
std::optional<double> nearest_key_time(
    const TrackRow& track,
    double target_time,
    double threshold_time);
std::vector<std::size_t> selected_indices(
    const std::vector<KeyRef>& selection,
    const TrackRow& track);
std::size_t clipboard_track_count(const Clipboard& clipboard);
std::optional<double> clipboard_time_shift(
    const Clipboard& clipboard,
    std::string_view animation_name,
    double playhead_time);
std::optional<double> snap_delta_to_frames(
    double earliest_time,
    double requested_delta,
    double frames_per_second);
std::optional<double> incremental_retime_delta(
    double requested_delta,
    double applied_delta);

enum class CompletionAction : std::uint8_t {
    Cancel,
    Commit,
};

struct CompletionDecision {
    CompletionAction action{CompletionAction::Cancel};
    bool report_cancelled{false};
    std::size_t history_entries{0U};
};

CompletionDecision completion_decision(
    bool commit_requested,
    bool changed);

template <typename Keyframe>
std::optional<double> insertable_key_time(
    const std::vector<Keyframe>& keyframes,
    double desired_time,
    double duration) {
    (void)duration;
    auto iterator = std::upper_bound(
        keyframes.begin(),
        keyframes.end(),
        desired_time,
        [](double time, const Keyframe& keyframe) {
            return time < keyframe.time;
        });
    const double minimum_time = iterator == keyframes.begin()
        ? 0.0
        : (iterator - 1)->time + kNonEventKeySpacing;
    if (iterator == keyframes.end()) {
        return std::max(desired_time, minimum_time);
    }
    const double maximum_time = iterator->time - kNonEventKeySpacing;
    if (maximum_time < minimum_time) {
        return std::nullopt;
    }
    return std::clamp(desired_time, minimum_time, maximum_time);
}

template <typename Keyframe>
double clamp_existing_key_time(
    const std::vector<Keyframe>& keyframes,
    std::size_t key_index_value,
    double desired_time,
    double duration) {
    (void)duration;
    const double minimum_time = key_index_value == 0U
        ? 0.0
        : keyframes[key_index_value - 1U].time + kNonEventKeySpacing;
    if (key_index_value + 1U >= keyframes.size()) {
        return std::max(desired_time, minimum_time);
    }
    const double maximum_time =
        keyframes[key_index_value + 1U].time - kNonEventKeySpacing;
    if (maximum_time < minimum_time) {
        return minimum_time;
    }
    return std::clamp(desired_time, minimum_time, maximum_time);
}

template <typename Keyframe>
double clamp_existing_non_decreasing_key_time(
    const std::vector<Keyframe>& keyframes,
    std::size_t key_index_value,
    double desired_time) {
    const double minimum_time = key_index_value == 0U
        ? 0.0
        : keyframes[key_index_value - 1U].time;
    if (key_index_value + 1U >= keyframes.size()) {
        return std::max(desired_time, minimum_time);
    }
    return std::clamp(
        desired_time, minimum_time, keyframes[key_index_value + 1U].time);
}

template <typename Timeline>
void append_selected_timeline_fragment(
    const Timeline& source,
    const std::vector<std::size_t>& indices,
    std::vector<Timeline>* destination) {
    if (destination == nullptr) {
        return;
    }
    Timeline copied = source;
    copied.keyframes.clear();
    for (const std::size_t index : indices) {
        if (index < source.keyframes.size()) {
            copied.keyframes.push_back(source.keyframes[index]);
        }
    }
    if (!copied.keyframes.empty()) {
        destination->push_back(std::move(copied));
    }
}

template <typename Key>
void paste_keys_replace_collisions(
    std::vector<Key>* destination,
    const std::vector<Key>& source,
    double time_shift,
    bool retain_same_time_source_order) {
    if (destination == nullptr || source.empty()) {
        return;
    }
    std::vector<double> target_times;
    for (const Key& key : source) {
        const double target_time = std::max(0.0, key.time + time_shift);
        if (std::find_if(
                target_times.begin(),
                target_times.end(),
                [&](double time) {
                    return std::abs(time - target_time) <= kKeyTimeEpsilon;
                }) == target_times.end()) {
            target_times.push_back(target_time);
        }
    }
    destination->erase(
        std::remove_if(
            destination->begin(),
            destination->end(),
            [&](const Key& key) {
                return std::any_of(
                    target_times.begin(),
                    target_times.end(),
                    [&](double time) {
                        return std::abs(key.time - time) <= kKeyTimeEpsilon;
                    });
            }),
        destination->end());
    for (Key key : source) {
        key.time = std::max(0.0, key.time + time_shift);
        destination->push_back(std::move(key));
    }
    const auto compare_time = [](const Key& left, const Key& right) {
        return left.time < right.time;
    };
    if (retain_same_time_source_order) {
        std::stable_sort(destination->begin(), destination->end(), compare_time);
    } else {
        std::sort(destination->begin(), destination->end(), compare_time);
    }
}

template <typename TimelineEdit>
void include_animation_timeline_maximum(
    const std::vector<TimelineEdit>& edits,
    std::string_view animation_name,
    double* maximum_time) {
    if (maximum_time == nullptr) {
        return;
    }
    for (const TimelineEdit& edit : edits) {
        if (edit.animation_name != animation_name) {
            continue;
        }
        for (const auto& keyframe : edit.keyframes) {
            *maximum_time = std::max(*maximum_time, keyframe.time);
        }
    }
}

struct RetimeBounds {
    double minimum_delta{-std::numeric_limits<double>::infinity()};
    double maximum_delta{std::numeric_limits<double>::infinity()};
};

template <typename Keyframe>
void include_retime_bounds(
    const std::vector<Keyframe>& keyframes,
    std::size_t key_index_value,
    double original_time,
    const std::set<std::size_t>& selected_indices_value,
    double spacing,
    RetimeBounds* bounds) {
    if (bounds == nullptr || key_index_value >= keyframes.size()) {
        return;
    }
    bounds->minimum_delta = std::max(bounds->minimum_delta, -original_time);
    for (std::size_t index = key_index_value; index > 0U; --index) {
        const std::size_t neighbor = index - 1U;
        if (selected_indices_value.find(neighbor) != selected_indices_value.end()) {
            continue;
        }
        bounds->minimum_delta = std::max(
            bounds->minimum_delta,
            keyframes[neighbor].time + spacing - original_time);
        break;
    }
    for (std::size_t neighbor = key_index_value + 1U;
         neighbor < keyframes.size();
         ++neighbor) {
        if (selected_indices_value.find(neighbor) != selected_indices_value.end()) {
            continue;
        }
        bounds->maximum_delta = std::min(
            bounds->maximum_delta,
            keyframes[neighbor].time - spacing - original_time);
        break;
    }
}

} // namespace marrow::editor::timeline_model

namespace marrow::editor::shell {

using TimelineTrackRow = marrow::editor::timeline_model::TrackRow;
using TimelineKeyRef = marrow::editor::timeline_model::KeyRef;
using TimelineClipboard = marrow::editor::timeline_model::Clipboard;

} // namespace marrow::editor::shell
