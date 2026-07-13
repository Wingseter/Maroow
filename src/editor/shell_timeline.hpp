#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/runtime/animation.hpp"

namespace marrow::runtime {
class Skeleton;
}

namespace marrow::editor::shell {

struct ShellState;
struct TimelineKeyRef;
struct TimelineTrackRow;
struct TimelineRetimeGesture;

template <typename Keyframe>
std::optional<double> insertable_key_time(
    const std::vector<Keyframe>& keyframes,
    double desired_time,
    double duration) {
    constexpr double kKeySpacing = 0.001;
    auto iterator = std::upper_bound(
        keyframes.begin(),
        keyframes.end(),
        desired_time,
        [](double time, const Keyframe& keyframe) {
            return time < keyframe.time;
        });
    const double minimum_time =
        iterator == keyframes.begin() ? 0.0 : (iterator - 1)->time + kKeySpacing;
    if (iterator == keyframes.end()) {
        return std::max(desired_time, minimum_time);
    }
    const double maximum_time = iterator->time - kKeySpacing;
    if (maximum_time < minimum_time) {
        return std::nullopt;
    }
    return std::clamp(desired_time, minimum_time, maximum_time);
}

std::optional<std::size_t> draw_order_position(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t slot_index);
std::string format_time_seconds(double time_seconds);
std::vector<double> collect_animation_key_times(
    const marrow::runtime::AnimationData& animation);
std::vector<TimelineTrackRow> build_timeline_tracks(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AnimationData& animation);
TimelineKeyRef timeline_key_ref(
    const TimelineTrackRow& track,
    std::size_t key_index);
std::optional<std::size_t> timeline_key_index(
    const TimelineTrackRow& track,
    const TimelineKeyRef& key);
void reconcile_timeline_key_selection(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
const TimelineTrackRow* selected_timeline_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);
bool timeline_track_matches_selection(
    const ShellState& state,
    const TimelineTrackRow& track);
bool add_timeline_key_at_playhead(
    ShellState* state,
    const TimelineTrackRow& track);
bool remove_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool copy_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool cut_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool paste_timeline_clipboard(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool begin_timeline_retime_gesture_for_smoke(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool apply_timeline_retime_delta(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_delta,
    bool snap_to_frames);
void finish_timeline_retime_gesture(ShellState* state, bool commit);
bool set_selected_animation(
    ShellState* state,
    std::string_view animation_name,
    std::string_view source,
    bool update_status_message,
    bool reset_time);
bool scrub_timeline_time(
    ShellState* state,
    double time_seconds,
    std::string_view source,
    bool update_status_message);
void advance_timeline_playback(ShellState* state, double delta_seconds);
void advance_timeline_playback(ShellState* state, float delta_seconds);
bool focus_timeline_track(
    ShellState* state,
    const TimelineTrackRow& track,
    double time_seconds,
    std::string_view source,
    bool update_status_message);
std::optional<std::size_t> ensure_transform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_mesh_deform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_draw_order_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_event_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_slot_color_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_slot_attachment_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
marrow::editor::TransformKeyframeEdit sample_transform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track);
void draw_draw_order_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_event_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_mesh_deform_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_slot_color_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_slot_attachment_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_transform_timeline_editor(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
void draw_timeline_window(ShellState* state);

} // namespace marrow::editor::shell
