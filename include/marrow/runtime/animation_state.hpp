#pragma once

#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::runtime {

class AnimationState;

enum class AnimationStateEventType {
    Start,
    Interrupt,
    End,
    Dispose,
    Complete,
    Event,
};

struct RootMotionDelta {
    double x{0.0};
    double y{0.0};
};

struct TrackEntry;

enum class AnimationStateTimelineMode {
    Subsequent,
    First,
    HoldSubsequent,
    HoldFirst,
    HoldMix,
};

using AnimationStateListener = std::function<void(
    AnimationState& state,
    AnimationStateEventType type,
    const std::shared_ptr<TrackEntry>& entry,
    const AnimationEvent* event)>;

struct TrackEntry : public std::enable_shared_from_this<TrackEntry> {
    std::size_t track_index{0};
    const AnimationData* animation{nullptr};
    std::string animation_name;
    bool loop{false};
    bool is_empty{false};
    bool reverse{false};
    double alpha{1.0};
    double mix_duration{0.0};
    double mix_time{0.0};
    double track_time{0.0};
    double track_last{-1.0};
    double next_track_last{-1.0};
    double track_end{std::numeric_limits<double>::infinity()};
    double delay{0.0};
    double interrupt_alpha{1.0};
    double total_alpha{0.0};
    AnimationStateListener listener;
    std::vector<AnimationStateTimelineMode> timeline_modes;
    std::vector<std::weak_ptr<TrackEntry>> timeline_hold_mix;
    std::vector<double> timelines_rotation;
    std::vector<BonePose> snapshot_bone_poses;
    std::vector<SlotState> snapshot_slot_states;
    std::vector<MeshDeformState> snapshot_mesh_deforms;
    std::vector<std::size_t> snapshot_draw_order;
    bool snapshot_frozen{false};
    std::shared_ptr<TrackEntry> next;
    std::shared_ptr<TrackEntry> mixing_from;
    std::weak_ptr<TrackEntry> mixing_to;

    double animation_duration() const;
    double animation_time() const;
    bool is_complete() const;
    void set_listener(AnimationStateListener value);

private:
    friend class AnimationState;

    AnimationState* owner_{nullptr};
    bool has_started_{false};
    bool start_notified_to_entry_{false};
    double last_track_time_{0.0};
};

class AnimationState {
public:
    explicit AnimationState(std::shared_ptr<const SkeletonData> data);

    const std::shared_ptr<const SkeletonData>& data() const;
    std::shared_ptr<TrackEntry> get_current(std::size_t track_index) const;

    std::shared_ptr<TrackEntry> set_animation(
        std::size_t track_index,
        std::string_view animation_name,
        bool loop,
        std::optional<double> mix_duration = std::nullopt);
    std::shared_ptr<TrackEntry> add_animation(
        std::size_t track_index,
        std::string_view animation_name,
        bool loop,
        double delay,
        std::optional<double> mix_duration = std::nullopt);
    std::shared_ptr<TrackEntry> set_empty_animation(
        std::size_t track_index,
        double mix_duration);
    std::shared_ptr<TrackEntry> add_empty_animation(
        std::size_t track_index,
        double mix_duration,
        double delay);

    void clear_track(std::size_t track_index);
    void clear_tracks();
    void update(double delta);
    void apply(Skeleton& skeleton);
    RootMotionDelta extract_root_motion(
        std::size_t track_index,
        std::size_t root_bone_index) const;

    void set_listener(AnimationStateListener value);

private:
    void ensure_track(std::size_t track_index);
    std::shared_ptr<TrackEntry> make_animation_entry(
        std::size_t track_index,
        std::string_view animation_name,
        bool loop) const;
    std::shared_ptr<TrackEntry> make_empty_entry(
        std::size_t track_index,
        double mix_duration) const;
    double resolve_mix_duration(
        const std::shared_ptr<TrackEntry>& from,
        std::string_view to_animation,
        std::optional<double> explicit_mix_duration) const;
    void replace_current(
        std::size_t track_index,
        const std::shared_ptr<TrackEntry>& entry,
        bool interrupt_current);
    bool update_mixing_from(
        const std::shared_ptr<TrackEntry>& entry,
        double delta);
    void advance_entry(
        const std::shared_ptr<TrackEntry>& entry,
        double delta);
    void dispatch_complete_callbacks(
        const std::shared_ptr<TrackEntry>& entry,
        double previous_time,
        double current_time);
    void dispatch_event_callbacks(
        const std::shared_ptr<TrackEntry>& entry,
        double previous_time,
        double current_time);
    void dispatch_event_range(
        const std::shared_ptr<TrackEntry>& entry,
        double start_time,
        double end_time);
    void refresh_timeline_modes();
    void set_timeline_modes(const std::shared_ptr<TrackEntry>& entry);
    void apply_current_timeline_values(
        Skeleton& skeleton,
        TrackEntry& entry,
        double alpha,
        bool use_only_setup_mix) const;
    void apply_discrete_timelines(
        Skeleton& skeleton,
        const TrackEntry& entry) const;
    void capture_entry_snapshot(
        const std::shared_ptr<TrackEntry>& entry,
        const Skeleton& skeleton) const;
    double apply_mixing_from(
        const std::shared_ptr<TrackEntry>& entry,
        Skeleton& skeleton) const;
    void prune_mixing_from(const std::shared_ptr<TrackEntry>& entry);
    void start_next_entry(std::size_t track_index);
    void dispose_queued_entries(const std::shared_ptr<TrackEntry>& entry);
    void dispose_entry_only(
        const std::shared_ptr<TrackEntry>& entry,
        bool dispatch_end);
    void dispose_active_entry(
        const std::shared_ptr<TrackEntry>& entry,
        bool dispatch_end);
    void dispatch(
        AnimationStateEventType type,
        const std::shared_ptr<TrackEntry>& entry,
        const AnimationEvent* event = nullptr);

    std::shared_ptr<const SkeletonData> data_;
    std::vector<std::shared_ptr<TrackEntry>> tracks_;
    AnimationStateListener listener_;
    bool animations_changed_{false};
    std::vector<std::string> property_ids_;
};

} // namespace marrow::runtime
