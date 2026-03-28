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

enum class AnimationLayerBlendMode {
    Override,
    Additive,
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
    // Track alpha is clamped to [0, 1] when applied.
    double alpha{1.0};
    AnimationLayerBlendMode blend_mode{AnimationLayerBlendMode::Override};
    // Empty means the layer can affect every animated bone on the track.
    std::vector<std::size_t> bone_filter;
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

    /// @brief Returns the duration of the assigned animation or empty mix.
    /// @return Animation duration in seconds.
    double animation_duration() const;
    /// @brief Returns the current sampled animation time for this entry.
    /// @return Current animation time in seconds.
    double animation_time() const;
    /// @brief Reports whether the entry has reached its end time.
    /// @return `true` when playback is complete; otherwise `false`.
    bool is_complete() const;
    /**
     * @brief Installs a listener that receives callbacks for this track entry.
     * @param value Listener to invoke for entry-level events.
     */
    void set_listener(AnimationStateListener value);

private:
    friend class AnimationState;

    AnimationState* owner_{nullptr};
    bool has_started_{false};
    bool start_notified_to_entry_{false};
    double last_track_time_{0.0};
};

struct AnimationStateTrackEntrySnapshot {
    std::size_t track_index{0};
    std::string animation_name;
    bool loop{false};
    bool is_empty{false};
    bool reverse{false};
    double alpha{1.0};
    AnimationLayerBlendMode blend_mode{AnimationLayerBlendMode::Override};
    std::vector<std::size_t> bone_filter;
    double mix_duration{0.0};
    double mix_time{0.0};
    double track_time{0.0};
    std::optional<double> track_last;
    std::optional<double> next_track_last;
    std::optional<double> track_end;
    double delay{0.0};
    double interrupt_alpha{1.0};
    double total_alpha{0.0};
    std::vector<double> timelines_rotation;
    std::vector<BonePose> snapshot_bone_poses;
    std::vector<SlotState> snapshot_slot_states;
    std::vector<MeshDeformState> snapshot_mesh_deforms;
    std::vector<std::size_t> snapshot_draw_order;
    bool snapshot_frozen{false};
    bool has_started{false};
    bool start_notified_to_entry{false};
    double last_track_time{0.0};
    std::optional<std::size_t> next_entry_index;
    std::optional<std::size_t> mixing_from_entry_index;
};

struct AnimationStateSnapshot {
    std::vector<std::optional<std::size_t>> track_roots;
    std::vector<AnimationStateTrackEntrySnapshot> entries;
};

class AnimationState {
public:
    // Mutable per-instance track and mixing state. AnimationState is not
    // internally synchronized; drive each instance from one thread at a time.
    // Distinct AnimationState + Skeleton pairs may update concurrently when
    // they only share immutable SkeletonData.
    /**
     * @brief Creates animation playback state for one immutable skeleton asset.
     * @param data Shared immutable skeleton data used for animation lookup.
     */
    explicit AnimationState(std::shared_ptr<const SkeletonData> data);

    /// @brief Returns the immutable skeleton data used by this state object.
    /// @return Shared skeleton data handle.
    const std::shared_ptr<const SkeletonData>& data() const;
    /**
     * @brief Returns the current entry on one track.
     * @param track_index Track index to inspect.
     * @return Current track entry, or `nullptr` when the track is empty.
     */
    std::shared_ptr<TrackEntry> get_current(std::size_t track_index) const;

    /**
     * @brief Replaces the current entry on a track with a named animation.
     * @param track_index Track index to update.
     * @param animation_name Animation name to play.
     * @param loop Whether the animation should loop.
     * @param mix_duration Optional explicit mix duration override.
     * @return The new current track entry.
     */
    std::shared_ptr<TrackEntry> set_animation(
        std::size_t track_index,
        std::string_view animation_name,
        bool loop,
        std::optional<double> mix_duration = std::nullopt);
    /**
     * @brief Queues a named animation after the current track chain.
     * @param track_index Track index to update.
     * @param animation_name Animation name to queue.
     * @param loop Whether the animation should loop.
     * @param delay Delay before the queued entry starts.
     * @param mix_duration Optional explicit mix duration override.
     * @return The queued track entry.
     */
    std::shared_ptr<TrackEntry> add_animation(
        std::size_t track_index,
        std::string_view animation_name,
        bool loop,
        double delay,
        std::optional<double> mix_duration = std::nullopt);
    /**
     * @brief Replaces the current track entry with an empty fade-out entry.
     * @param track_index Track index to update.
     * @param mix_duration Duration of the empty fade-out.
     * @return The new empty entry.
     */
    std::shared_ptr<TrackEntry> set_empty_animation(
        std::size_t track_index,
        double mix_duration);
    /**
     * @brief Queues an empty fade-out entry after the current track chain.
     * @param track_index Track index to update.
     * @param mix_duration Duration of the empty fade-out.
     * @param delay Delay before the empty entry starts.
     * @return The queued empty entry.
     */
    std::shared_ptr<TrackEntry> add_empty_animation(
        std::size_t track_index,
        double mix_duration,
        double delay);

    /// @brief Captures a serializable snapshot of the current animation-state graph.
    /// @return Snapshot containing tracks, entries, and snapshot pose state.
    AnimationStateSnapshot capture_state() const;
    /**
     * @brief Restores animation-state playback from a previously captured snapshot.
     * @param snapshot Snapshot to restore.
     */
    void restore_state(const AnimationStateSnapshot& snapshot);

    /**
     * @brief Clears a single track and disposes its queued entries.
     * @param track_index Track index to clear.
     */
    void clear_track(std::size_t track_index);
    /// @brief Clears every animation track managed by this state object.
    void clear_tracks();
    /**
     * @brief Advances track playback times without applying them to a skeleton.
     * @param delta Time step in seconds.
     */
    void update(double delta);
    /**
     * @brief Applies the current track state to a skeleton and updates world transforms.
     * @param skeleton Skeleton instance created from the same `SkeletonData`.
     */
    void apply(Skeleton& skeleton);
    /**
     * @brief Applies the current track state to a skeleton without world-transform evaluation.
     * @param skeleton Skeleton instance created from the same `SkeletonData`.
     */
    void apply_pose(Skeleton& skeleton);
    /**
     * @brief Extracts root-motion delta for one track and root bone.
     * @param track_index Track index to sample.
     * @param root_bone_index Root bone used for motion extraction.
     * @return Root-motion delta accumulated by the track.
     */
    RootMotionDelta extract_root_motion(
        std::size_t track_index,
        std::size_t root_bone_index) const;

    /**
     * @brief Installs a listener that receives callbacks for all track events.
     * @param value Listener to invoke for animation-state events.
     */
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
