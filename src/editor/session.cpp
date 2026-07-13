#include "marrow/editor/session.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "marrow/allocator.hpp"
#include "marrow/editor/authoring.hpp"

namespace marrow::editor {
namespace {

constexpr std::size_t kMaximumHistoryDepth = 100U;
constexpr double kPreviewStepSeconds = 1.0 / 60.0;

bool attachment_override_equal(
    const std::optional<PreviewAttachmentOverride>& left,
    const std::optional<PreviewAttachmentOverride>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }
    return left->skin_index == right->skin_index &&
        left->attachment_name == right->attachment_name;
}

bool attachment_overrides_equal(
    const std::vector<std::optional<PreviewAttachmentOverride>>& left,
    const std::vector<std::optional<PreviewAttachmentOverride>>& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!attachment_override_equal(left[index], right[index])) {
            return false;
        }
    }
    return true;
}

bool preview_states_equal(const PreviewState& left, const PreviewState& right) {
    return left.animation_name == right.animation_name &&
        left.time_seconds == right.time_seconds &&
        left.loop == right.loop &&
        left.playing == right.playing &&
        left.queue_enabled == right.queue_enabled &&
        left.queued_animation_name == right.queued_animation_name &&
        left.queue_delay == right.queue_delay &&
        left.mix_duration == right.mix_duration &&
        left.reverse == right.reverse &&
        left.skin_names == right.skin_names &&
        attachment_overrides_equal(left.slot_overrides, right.slot_overrides);
}

bool preview_playback_equal(const PreviewState& left, const PreviewState& right) {
    return left.animation_name == right.animation_name &&
        left.time_seconds == right.time_seconds &&
        left.loop == right.loop &&
        left.playing == right.playing &&
        left.queue_enabled == right.queue_enabled &&
        left.queued_animation_name == right.queued_animation_name &&
        left.queue_delay == right.queue_delay &&
        left.mix_duration == right.mix_duration &&
        left.reverse == right.reverse;
}

std::optional<std::size_t> root_bone_index(const runtime::SkeletonData& data) {
    if (const auto named_root = data.find_bone_index("root")) {
        return named_root;
    }
    for (std::size_t index = 0; index < data.bones().size(); ++index) {
        if (!data.bones()[index].parent_index.has_value()) {
            return index;
        }
    }
    return data.bones().empty() ? std::nullopt : std::optional<std::size_t>(0U);
}

runtime::json::LoadError make_session_load_error(
    const std::filesystem::path& path,
    std::string message) {
    runtime::json::LoadError error;
    error.source_path = path;
    error.message = std::move(message);
    return error;
}

ProjectSaveResult make_save_failure(
    const std::filesystem::path& path,
    std::string message) {
    ProjectSaveResult result;
    result.error = ProjectSaveError{path, std::move(message)};
    return result;
}

ProjectExportResult make_export_failure(
    const std::filesystem::path& path,
    std::string message) {
    ProjectExportResult result;
    result.path = path;
    result.error = ProjectExportError{path, std::move(message)};
    return result;
}

class PreviewController {
public:
    struct Snapshot {
        PreviewState state;
        runtime::RootMotionDelta root_motion_delta{};
        runtime::RootMotionDelta root_motion_total{};
        std::vector<runtime::AnimationEvent> events;
    };

    PreviewController() = default;
    PreviewController(PreviewController&&) noexcept = default;
    PreviewController& operator=(PreviewController&&) noexcept = default;
    PreviewController(const PreviewController&) = delete;
    PreviewController& operator=(const PreviewController&) = delete;

    bool bind(
        std::shared_ptr<const runtime::SkeletonData> data,
        PreviewState requested_state,
        bool preserve_root_motion,
        std::string* error_out) {
        if (data == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Preview runtime data is not available.";
            }
            return false;
        }

        normalize_state(*data, &requested_state);
        auto next_skeleton = marrow::allocate_unique<runtime::Skeleton>(data);
        auto next_animation_state =
            marrow::allocate_unique<runtime::AnimationState>(data);

        const runtime::RootMotionDelta previous_total = root_motion_total_;
        data_ = std::move(data);
        state_ = std::move(requested_state);
        skeleton_ = std::move(next_skeleton);
        animation_state_ = std::move(next_animation_state);
        root_motion_delta_ = {};
        root_motion_total_ = preserve_root_motion ? previous_total : runtime::RootMotionDelta{};
        events_.clear();

        if (!refresh_pose(false, error_out)) {
            data_.reset();
            skeleton_.reset();
            animation_state_.reset();
            return false;
        }
        return true;
    }

    const PreviewState& state() const noexcept { return state_; }
    const runtime::Skeleton* skeleton() const noexcept { return skeleton_.get(); }
    runtime::Skeleton* mutable_skeleton() noexcept { return skeleton_.get(); }
    const runtime::AnimationState* animation_state() const noexcept {
        return animation_state_.get();
    }
    runtime::AnimationState* mutable_animation_state() noexcept {
        return animation_state_.get();
    }
    bool restore_playback(
        const runtime::AnimationStateSnapshot& snapshot,
        std::string* error_out) {
        if (skeleton_ == nullptr || animation_state_ == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Preview runtime data is not available.";
            }
            return false;
        }
        for (const auto& entry : snapshot.entries) {
            const bool invalid_animation =
                !entry.is_empty && data_->find_animation(entry.animation_name) == nullptr;
            const bool invalid_bone_filter = std::any_of(
                entry.bone_filter.begin(),
                entry.bone_filter.end(),
                [&](std::size_t bone_index) { return bone_index >= data_->bones().size(); });
            const bool incompatible_pose =
                (!entry.snapshot_bone_poses.empty() &&
                 entry.snapshot_bone_poses.size() != data_->bones().size()) ||
                (!entry.snapshot_slot_states.empty() &&
                 entry.snapshot_slot_states.size() != data_->slots().size()) ||
                (!entry.snapshot_mesh_deforms.empty() &&
                 entry.snapshot_mesh_deforms.size() != data_->slots().size()) ||
                (!entry.snapshot_draw_order.empty() &&
                 entry.snapshot_draw_order.size() != data_->slots().size());
            if (invalid_animation || invalid_bone_filter || incompatible_pose) {
                // Hot reload may legitimately change animations or topology.
                // The normalized public preview settings are still valid, so
                // rebuild their track graph instead of restoring a partial one.
                return refresh_pose(false, error_out);
            }
        }
        animation_state_->restore_state(snapshot);
        skeleton_->set_to_setup_pose();
        skeleton_->set_attachment_playback_time(state_.time_seconds);
        animation_state_->apply(*skeleton_);
        apply_slot_overrides();
        return true;
    }
    const std::vector<runtime::AnimationEvent>& events() const noexcept { return events_; }
    runtime::RootMotionDelta root_motion_delta() const noexcept { return root_motion_delta_; }
    runtime::RootMotionDelta root_motion_total() const noexcept { return root_motion_total_; }

    Snapshot capture() const {
        return Snapshot{state_, root_motion_delta_, root_motion_total_, events_};
    }

    void restore_transient_state(const Snapshot& snapshot) {
        root_motion_delta_ = snapshot.root_motion_delta;
        root_motion_total_ = snapshot.root_motion_total;
        events_ = snapshot.events;
    }

    bool restore(const Snapshot& snapshot, std::string* error_out) {
        if (data_ == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Preview runtime data is not available.";
            }
            return false;
        }
        const runtime::RootMotionDelta desired_delta = snapshot.root_motion_delta;
        const runtime::RootMotionDelta desired_total = snapshot.root_motion_total;
        const std::vector<runtime::AnimationEvent> desired_events = snapshot.events;
        state_ = snapshot.state;
        normalize_state(*data_, &state_);
        if (!refresh_pose(false, error_out)) {
            return false;
        }
        root_motion_delta_ = desired_delta;
        root_motion_total_ = desired_total;
        events_ = desired_events;
        return true;
    }

    bool select_animation(
        std::string_view animation_name,
        bool reset_time,
        std::string* error_out) {
        if (data_ == nullptr || data_->find_animation(animation_name) == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Unknown preview animation '" + std::string(animation_name) + "'.";
            }
            return false;
        }
        const PreviewState previous = state_;
        state_.animation_name = std::string(animation_name);
        if (reset_time) {
            state_.time_seconds = 0.0;
        }
        normalize_state(*data_, &state_);
        if (!refresh_pose(true, error_out)) {
            state_ = previous;
            refresh_pose(false, nullptr);
            return false;
        }
        return true;
    }

    bool select_setup_pose(std::string* error_out) {
        if (data_ == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Preview runtime data is not available.";
            }
            return false;
        }
        const PreviewState previous = state_;
        state_.animation_name.clear();
        state_.time_seconds = 0.0;
        state_.playing = false;
        state_.queue_enabled = false;
        state_.queued_animation_name.clear();
        state_.queue_delay = 0.0;
        state_.mix_duration.reset();
        if (!refresh_pose(true, error_out)) {
            state_ = previous;
            refresh_pose(false, nullptr);
            return false;
        }
        return true;
    }

    bool seek(double time_seconds, std::string* error_out) {
        if (data_ == nullptr || !std::isfinite(time_seconds)) {
            if (error_out != nullptr) {
                *error_out = "Preview time must be finite.";
            }
            return false;
        }
        const double previous_time = state_.time_seconds;
        state_.time_seconds = std::max(0.0, time_seconds);
        normalize_state(*data_, &state_);
        if (!refresh_pose(true, error_out)) {
            state_.time_seconds = previous_time;
            refresh_pose(false, nullptr);
            return false;
        }
        return true;
    }

    bool set_loop(bool loop, std::string* error_out) {
        if (state_.loop == loop) {
            return true;
        }
        state_.loop = loop;
        return refresh_pose(false, error_out);
    }

    bool set_reverse(bool reverse, std::string* error_out) {
        if (state_.reverse == reverse) {
            return true;
        }
        state_.reverse = reverse;
        return refresh_pose(false, error_out);
    }

    bool set_queue(
        std::string_view animation_name,
        double delay_seconds,
        std::optional<double> mix_duration,
        std::string* error_out) {
        if (data_ == nullptr || data_->find_animation(animation_name) == nullptr ||
            animation_name == state_.animation_name || !std::isfinite(delay_seconds) ||
            (mix_duration.has_value() && !std::isfinite(*mix_duration))) {
            if (error_out != nullptr) {
                *error_out = "Invalid queued preview animation settings.";
            }
            return false;
        }
        const PreviewState previous = state_;
        state_.queue_enabled = true;
        state_.queued_animation_name = std::string(animation_name);
        state_.queue_delay = std::max(0.0, delay_seconds);
        state_.mix_duration = mix_duration.has_value()
            ? std::optional<double>(std::max(0.0, *mix_duration))
            : std::nullopt;
        normalize_state(*data_, &state_);
        if (!refresh_pose(false, error_out)) {
            state_ = previous;
            refresh_pose(false, nullptr);
            return false;
        }
        return true;
    }

    bool clear_queue(std::string* error_out) {
        if (!state_.queue_enabled) {
            return true;
        }
        state_.queue_enabled = false;
        return refresh_pose(false, error_out);
    }

    void set_playing(bool playing) noexcept { state_.playing = playing; }

    bool set_skins(std::vector<std::string> skin_names, std::string* error_out) {
        if (data_ == nullptr) {
            return false;
        }
        for (const std::string& skin_name : skin_names) {
            if (!data_->find_skin_index(skin_name).has_value()) {
                if (error_out != nullptr) {
                    *error_out = "Unknown preview skin '" + skin_name + "'.";
                }
                return false;
            }
        }
        remove_duplicate_skins(&skin_names);
        if (state_.skin_names == skin_names) {
            return true;
        }
        const auto previous = state_.skin_names;
        state_.skin_names = std::move(skin_names);
        if (!refresh_pose(false, error_out)) {
            state_.skin_names = previous;
            refresh_pose(false, nullptr);
            return false;
        }
        return true;
    }

    bool set_attachment(
        std::size_t slot_index,
        std::optional<std::size_t> skin_index,
        std::string attachment_name,
        std::string* error_out) {
        if (!validate_attachment(slot_index, skin_index, attachment_name, error_out)) {
            return false;
        }
        if (state_.slot_overrides.size() < data_->slots().size()) {
            state_.slot_overrides.resize(data_->slots().size());
        }
        const std::optional<PreviewAttachmentOverride> next_override =
            PreviewAttachmentOverride{skin_index, std::move(attachment_name)};
        if (attachment_override_equal(state_.slot_overrides[slot_index], next_override)) {
            return true;
        }
        state_.slot_overrides[slot_index] = next_override;
        apply_slot_overrides();
        return true;
    }

    bool reset_attachment(std::size_t slot_index, std::string* error_out) {
        if (data_ == nullptr || slot_index >= data_->slots().size()) {
            if (error_out != nullptr) {
                *error_out = "Preview slot index is out of range.";
            }
            return false;
        }
        if (state_.slot_overrides.size() < data_->slots().size()) {
            state_.slot_overrides.resize(data_->slots().size());
        }
        if (!state_.slot_overrides[slot_index].has_value()) {
            return true;
        }
        state_.slot_overrides[slot_index].reset();
        return refresh_pose(false, error_out);
    }

    bool advance(double delta_seconds, std::string* error_out) {
        if (!state_.playing || delta_seconds <= 0.0) {
            return false;
        }
        if (!std::isfinite(delta_seconds) || data_ == nullptr || skeleton_ == nullptr ||
            animation_state_ == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Preview playback cannot advance in its current state.";
            }
            return false;
        }

        const double duration = preview_duration();
        if (duration <= 0.0 || state_.animation_name.empty()) {
            state_.playing = false;
            return false;
        }

        double next_time = state_.time_seconds + delta_seconds;
        if (state_.loop) {
            next_time = std::fmod(next_time, duration);
            if (next_time < 0.0) {
                next_time += duration;
            }
        } else if (next_time >= duration) {
            next_time = duration;
            state_.playing = false;
        }
        state_.time_seconds = next_time;
        events_.clear();
        root_motion_delta_ = {};
        install_event_listener();
        animation_state_->update(delta_seconds);
        accumulate_root_motion();
        skeleton_->set_attachment_playback_time(state_.time_seconds);
        animation_state_->apply(*skeleton_);
        animation_state_->set_listener({});
        apply_slot_overrides();
        skeleton_->update_physics(delta_seconds);
        return true;
    }

private:
    static void remove_duplicate_skins(std::vector<std::string>* skin_names) {
        std::vector<std::string> unique;
        unique.reserve(skin_names->size());
        for (std::string& name : *skin_names) {
            if (std::find(unique.begin(), unique.end(), name) == unique.end()) {
                unique.push_back(std::move(name));
            }
        }
        *skin_names = std::move(unique);
    }

    static void normalize_state(const runtime::SkeletonData& data, PreviewState* state) {
        std::vector<std::string> valid_skins;
        valid_skins.reserve(state->skin_names.size());
        for (std::string& skin_name : state->skin_names) {
            if (data.find_skin_index(skin_name).has_value() &&
                std::find(valid_skins.begin(), valid_skins.end(), skin_name) ==
                    valid_skins.end()) {
                valid_skins.push_back(std::move(skin_name));
            }
        }
        state->skin_names = std::move(valid_skins);

        if (!state->animation_name.empty() &&
            data.find_animation(state->animation_name) == nullptr) {
            state->animation_name =
                data.animations().empty() ? std::string{} : data.animations().front().name;
        }

        if (state->queue_delay < 0.0 || !std::isfinite(state->queue_delay)) {
            state->queue_delay = 0.0;
        }
        if (state->mix_duration.has_value()) {
            if (!std::isfinite(*state->mix_duration)) {
                state->mix_duration.reset();
            } else if (*state->mix_duration < 0.0) {
                state->mix_duration = 0.0;
            }
        }
        if (data.find_animation(state->queued_animation_name) == nullptr ||
            state->queued_animation_name == state->animation_name) {
            state->queued_animation_name.clear();
            for (const auto& animation : data.animations()) {
                if (animation.name != state->animation_name) {
                    state->queued_animation_name = animation.name;
                    break;
                }
            }
        }
        if (state->queued_animation_name.empty()) {
            state->queue_enabled = false;
        }

        state->slot_overrides.resize(data.slots().size());
        for (std::size_t slot_index = 0; slot_index < state->slot_overrides.size(); ++slot_index) {
            auto& override_value = state->slot_overrides[slot_index];
            if (!override_value.has_value()) {
                continue;
            }
            const runtime::AttachmentData* attachment = override_value->skin_index.has_value()
                ? data.find_attachment(
                      *override_value->skin_index,
                      slot_index,
                      override_value->attachment_name)
                : data.find_attachment_source(
                      slot_index,
                      override_value->attachment_name,
                      &override_value->skin_index);
            if (attachment == nullptr) {
                override_value.reset();
            }
        }

        const runtime::AnimationData* animation = data.find_animation(state->animation_name);
        const double primary_duration =
            animation != nullptr ? std::max(0.0, animation->duration()) : 0.0;
        double duration = primary_duration;
        if (state->queue_enabled) {
            if (const auto* queued = data.find_animation(state->queued_animation_name)) {
                duration += state->queue_delay + std::max(0.0, queued->duration());
            }
        }
        state->time_seconds = std::isfinite(state->time_seconds)
            ? std::clamp(state->time_seconds, 0.0, duration)
            : 0.0;
    }

    double preview_duration() const {
        if (data_ == nullptr) {
            return 0.0;
        }
        const runtime::AnimationData* animation = data_->find_animation(state_.animation_name);
        double duration = animation != nullptr ? std::max(0.0, animation->duration()) : 0.0;
        if (state_.queue_enabled) {
            if (const auto* queued = data_->find_animation(state_.queued_animation_name)) {
                duration += state_.queue_delay + std::max(0.0, queued->duration());
            }
        }
        return duration;
    }

    bool validate_attachment(
        std::size_t slot_index,
        std::optional<std::size_t>* skin_index,
        std::string_view attachment_name,
        std::string* error_out) const {
        if (data_ == nullptr || slot_index >= data_->slots().size() || attachment_name.empty()) {
            if (error_out != nullptr) {
                *error_out = "Invalid preview attachment selection.";
            }
            return false;
        }
        const runtime::AttachmentData* attachment = skin_index->has_value()
            ? data_->find_attachment(**skin_index, slot_index, attachment_name)
            : data_->find_attachment_source(slot_index, attachment_name, skin_index);
        if (attachment == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Unknown preview attachment '" + std::string(attachment_name) + "'.";
            }
            return false;
        }
        return true;
    }

    bool validate_attachment(
        std::size_t slot_index,
        std::optional<std::size_t> skin_index,
        std::string_view attachment_name,
        std::string* error_out) const {
        return validate_attachment(slot_index, &skin_index, attachment_name, error_out);
    }

    void install_event_listener() {
        animation_state_->set_listener(
            [this](
                runtime::AnimationState&,
                runtime::AnimationStateEventType type,
                const std::shared_ptr<runtime::TrackEntry>&,
                const runtime::AnimationEvent* event) {
                if (type == runtime::AnimationStateEventType::Event && event != nullptr) {
                    events_.push_back(*event);
                }
            });
    }

    void accumulate_root_motion() {
        if (const auto root_index = root_bone_index(*data_)) {
            root_motion_delta_ = animation_state_->extract_root_motion(0U, *root_index);
            root_motion_total_.x += root_motion_delta_.x;
            root_motion_total_.y += root_motion_delta_.y;
        }
    }

    bool refresh_pose(bool reset_root_motion, std::string* error_out) {
        if (data_ == nullptr || skeleton_ == nullptr || animation_state_ == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Preview runtime data is not available.";
            }
            return false;
        }

        std::vector<std::string_view> skin_names;
        skin_names.reserve(state_.skin_names.size());
        for (const std::string& skin_name : state_.skin_names) {
            skin_names.push_back(skin_name);
        }
        if (!skeleton_->set_skin_composition(skin_names)) {
            if (error_out != nullptr) {
                *error_out = "Failed to apply the requested preview skin composition.";
            }
            return false;
        }

        const std::vector<runtime::AnimationEvent> preserved_events = events_;
        const runtime::RootMotionDelta preserved_delta = root_motion_delta_;
        const runtime::RootMotionDelta preserved_total = root_motion_total_;
        events_.clear();
        root_motion_delta_ = {};
        if (reset_root_motion) {
            root_motion_total_ = {};
        }
        animation_state_->set_listener({});
        animation_state_->clear_tracks();

        if (state_.animation_name.empty()) {
            state_.time_seconds = 0.0;
            skeleton_->set_to_setup_pose();
            skeleton_->set_attachment_playback_time(0.0);
            apply_slot_overrides();
            if (!reset_root_motion) {
                events_ = preserved_events;
                root_motion_delta_ = preserved_delta;
                root_motion_total_ = preserved_total;
            }
            return true;
        }

        const auto* animation = data_->find_animation(state_.animation_name);
        if (animation == nullptr) {
            if (error_out != nullptr) {
                *error_out = "Selected preview animation is no longer available.";
            }
            return false;
        }

        install_event_listener();
        const bool primary_loop = state_.queue_enabled ? false : state_.loop;
        auto current = animation_state_->set_animation(
            0U,
            animation->name,
            primary_loop,
            0.0);
        current->reverse = state_.reverse;
        current->alpha = 1.0;
        if (state_.queue_enabled) {
            if (const auto* queued = data_->find_animation(state_.queued_animation_name)) {
                auto queued_entry = animation_state_->add_animation(
                    0U,
                    queued->name,
                    false,
                    state_.queue_delay,
                    state_.mix_duration);
                queued_entry->reverse = state_.reverse;
            }
        }

        double elapsed = 0.0;
        while ((elapsed + kPreviewStepSeconds) < (state_.time_seconds - 1e-9)) {
            animation_state_->update(kPreviewStepSeconds);
            accumulate_root_motion();
            elapsed += kPreviewStepSeconds;
        }
        const double final_step = state_.time_seconds - elapsed;
        if (final_step > 1e-9) {
            animation_state_->update(final_step);
            accumulate_root_motion();
        }
        skeleton_->set_attachment_playback_time(state_.time_seconds);
        animation_state_->apply(*skeleton_);
        animation_state_->set_listener({});
        apply_slot_overrides();
        if (!reset_root_motion) {
            events_ = preserved_events;
            root_motion_delta_ = preserved_delta;
            root_motion_total_ = preserved_total;
        }
        return true;
    }

    void apply_slot_overrides() {
        if (data_ == nullptr || skeleton_ == nullptr) {
            return;
        }
        auto& slots = skeleton_->slot_states();
        auto& deforms = skeleton_->mesh_deform_states();
        for (std::size_t slot_index = 0;
             slot_index < state_.slot_overrides.size() && slot_index < slots.size();
             ++slot_index) {
            auto& override_value = state_.slot_overrides[slot_index];
            if (!override_value.has_value()) {
                continue;
            }
            std::optional<std::size_t> resolved_skin = override_value->skin_index;
            if (!validate_attachment(
                    slot_index,
                    &resolved_skin,
                    override_value->attachment_name,
                    nullptr)) {
                override_value.reset();
                continue;
            }
            override_value->skin_index = resolved_skin;
            slots[slot_index].attachment_name = override_value->attachment_name;
            slots[slot_index].attachment_skin_index = resolved_skin;
            if (slot_index < deforms.size() &&
                deforms[slot_index].attachment_name != override_value->attachment_name) {
                deforms[slot_index].attachment_name.clear();
                deforms[slot_index].vertex_offsets.clear();
            }
        }
    }

    std::shared_ptr<const runtime::SkeletonData> data_;
    PreviewState state_;
    marrow::UniquePtr<runtime::Skeleton> skeleton_;
    marrow::UniquePtr<runtime::AnimationState> animation_state_;
    std::vector<runtime::AnimationEvent> events_;
    runtime::RootMotionDelta root_motion_delta_{};
    runtime::RootMotionDelta root_motion_total_{};
};

} // namespace

struct EditorSession::Impl {
    struct HistorySnapshot {
        ProjectData project;
        std::string serialized_project;
        PreviewState preview_state;
    };

    struct HistoryEntry {
        EditDescriptor descriptor;
        HistorySnapshot before;
        HistorySnapshot after;
    };

    struct ActiveTransaction {
        std::uint64_t id{0U};
        EditDescriptor descriptor;
        HistorySnapshot before_history;
        PreviewController::Snapshot before_preview;
        std::shared_ptr<const runtime::SkeletonData> before_runtime;
        runtime::AnimationStateSnapshot before_playback;
        std::optional<PreviewState> pending_preview_state;
        bool runtime_is_current{false};
        bool live_refresh_applied{false};
    };

    ProjectLoadResult load;
    PreviewController preview;
    std::vector<HistoryEntry> undo_entries;
    std::vector<HistoryEntry> redo_entries;
    std::optional<ActiveTransaction> active_transaction;
    std::uint64_t next_transaction_id{1U};
    std::string saved_serialized_project;
    bool project_dirty{false};
    std::uint64_t project_revision{0U};
    std::uint64_t runtime_revision{0U};
    std::uint64_t preview_revision{0U};

    bool loaded() const noexcept {
        return static_cast<bool>(load) && load.project != nullptr &&
            load.base_skeleton_document != nullptr && load.skeleton_data != nullptr;
    }

    HistorySnapshot capture_history() const {
        HistorySnapshot snapshot;
        if (load.project != nullptr) {
            snapshot.project = *load.project;
            snapshot.serialized_project = serialize_project(*load.project);
        }
        snapshot.preview_state = preview.state();
        return snapshot;
    }

    static bool histories_equal(const HistorySnapshot& left, const HistorySnapshot& right) {
        return left.serialized_project == right.serialized_project &&
            left.project.source_path == right.project.source_path &&
            preview_states_equal(left.preview_state, right.preview_state);
    }

    void update_dirty() {
        project_dirty = load.project != nullptr &&
            serialize_project(*load.project) != saved_serialized_project;
    }

    static SessionError make_error(
        SessionErrorCode code,
        std::string message,
        std::optional<runtime::json::LoadError> detail = std::nullopt) {
        return SessionError{code, std::move(message), std::move(detail)};
    }

    bool set_preview_skins(std::vector<std::string> skin_names, SessionError* error_out) {
        std::string message;
        if (!preview.set_skins(std::move(skin_names), &message)) {
            if (error_out != nullptr) {
                *error_out = make_error(SessionErrorCode::PreviewUpdateFailed, std::move(message));
            }
            return false;
        }
        return true;
    }

    bool set_preview_attachment(
        std::size_t slot_index,
        std::optional<std::size_t> skin_index,
        std::string attachment_name,
        SessionError* error_out) {
        std::string message;
        if (!preview.set_attachment(
                slot_index,
                skin_index,
                std::move(attachment_name),
                &message)) {
            if (error_out != nullptr) {
                *error_out = make_error(SessionErrorCode::PreviewUpdateFailed, std::move(message));
            }
            return false;
        }
        return true;
    }

    bool reset_preview_attachment(std::size_t slot_index, SessionError* error_out) {
        std::string message;
        if (!preview.reset_attachment(slot_index, &message)) {
            if (error_out != nullptr) {
                *error_out = make_error(SessionErrorCode::PreviewUpdateFailed, std::move(message));
            }
            return false;
        }
        return true;
    }

    void restore_active_transaction() noexcept {
        if (!active_transaction.has_value() || load.project == nullptr) {
            active_transaction.reset();
            return;
        }
        ActiveTransaction transaction = std::move(*active_transaction);
        *load.project = transaction.before_history.project;
        if (transaction.live_refresh_applied && transaction.before_runtime != nullptr) {
            PreviewController restored_preview;
            std::string ignored_error;
            if (restored_preview.bind(
                    transaction.before_runtime,
                    transaction.before_preview.state,
                    false,
                    &ignored_error) &&
                restored_preview.restore_playback(
                    transaction.before_playback,
                    &ignored_error)) {
                restored_preview.restore_transient_state(transaction.before_preview);
                load.skeleton_data = std::move(transaction.before_runtime);
                preview = std::move(restored_preview);
                ++runtime_revision;
                ++preview_revision;
            }
        } else {
            preview.restore(transaction.before_preview, nullptr);
        }
        active_transaction.reset();
        update_dirty();
    }

    SessionResult refresh_runtime(std::uint64_t transaction_id) {
        if (!active_transaction.has_value() ||
            active_transaction->id != transaction_id) {
            return SessionResult{
                false,
                make_error(
                    SessionErrorCode::InvalidTransaction,
                    "The edit transaction is no longer active.")};
        }

        const ProjectRuntimeResult runtime_result = build_project_runtime(
            *load.project,
            *load.base_skeleton_document);
        if (!runtime_result) {
            return SessionResult{
                false,
                make_error(
                    SessionErrorCode::RuntimeBuildFailed,
                    "The live edit did not produce valid runtime data.",
                    runtime_result.error)};
        }

        const PreviewController::Snapshot current_preview = preview.capture();
        const PreviewState desired_preview =
            active_transaction->pending_preview_state.value_or(current_preview.state);
        const bool playback_changed =
            !preview_playback_equal(current_preview.state, desired_preview);
        const runtime::AnimationStateSnapshot playback_snapshot =
            preview.animation_state()->capture_state();
        PreviewController next_preview;
        std::string preview_error;
        if (!next_preview.bind(
                runtime_result.skeleton_data,
                desired_preview,
                !playback_changed,
                &preview_error) ||
            (!playback_changed &&
             !next_preview.restore_playback(playback_snapshot, &preview_error))) {
            return SessionResult{
                false,
                make_error(
                    SessionErrorCode::PreviewUpdateFailed,
                    std::move(preview_error))};
        }
        if (!playback_changed) {
            next_preview.restore_transient_state(current_preview);
        }

        load.skeleton_data = runtime_result.skeleton_data;
        preview = std::move(next_preview);
        active_transaction->runtime_is_current = true;
        active_transaction->live_refresh_applied = true;
        update_dirty();
        ++runtime_revision;
        ++preview_revision;
        return SessionResult{true, std::nullopt};
    }

    void push_history(HistoryEntry entry) {
        redo_entries.clear();
        if (!undo_entries.empty() && entry.descriptor.allow_merge &&
            !entry.descriptor.merge_key.empty()) {
            HistoryEntry& previous = undo_entries.back();
            if (previous.descriptor.allow_merge &&
                previous.descriptor.kind == entry.descriptor.kind &&
                previous.descriptor.merge_key == entry.descriptor.merge_key) {
                previous.after = std::move(entry.after);
                previous.descriptor.label = std::move(entry.descriptor.label);
                previous.descriptor.impacts =
                    previous.descriptor.impacts | entry.descriptor.impacts;
                if (histories_equal(previous.before, previous.after)) {
                    undo_entries.pop_back();
                }
                return;
            }
        }
        if (undo_entries.size() >= kMaximumHistoryDepth) {
            undo_entries.erase(undo_entries.begin());
        }
        undo_entries.push_back(std::move(entry));
    }

    SessionResult commit(std::uint64_t transaction_id) {
        if (!active_transaction.has_value() || active_transaction->id != transaction_id) {
            return SessionResult{
                false,
                make_error(
                    SessionErrorCode::InvalidTransaction,
                    "The edit transaction is no longer active.")};
        }

        ActiveTransaction transaction = *active_transaction;
        HistorySnapshot requested_after = capture_history();
        if (transaction.pending_preview_state.has_value()) {
            requested_after.preview_state = *transaction.pending_preview_state;
        }
        if (histories_equal(transaction.before_history, requested_after)) {
            active_transaction.reset();
            return {};
        }

        const bool project_changed =
            transaction.before_history.serialized_project != requested_after.serialized_project ||
            transaction.before_history.project.source_path != requested_after.project.source_path;
        const bool preview_changed = !preview_states_equal(
            transaction.before_history.preview_state,
            requested_after.preview_state);

        std::shared_ptr<const runtime::SkeletonData> next_runtime = load.skeleton_data;
        if (project_changed && !transaction.runtime_is_current) {
            const ProjectRuntimeResult runtime_result = build_project_runtime(
                *load.project,
                *load.base_skeleton_document);
            if (!runtime_result) {
                *load.project = transaction.before_history.project;
                preview.restore(transaction.before_preview, nullptr);
                active_transaction.reset();
                update_dirty();
                return SessionResult{
                    false,
                    make_error(
                        SessionErrorCode::RuntimeBuildFailed,
                        "The edit did not produce valid runtime data.",
                        runtime_result.error)};
            }
            next_runtime = runtime_result.skeleton_data;
        }

        if (project_changed &&
            has_edit_impact(transaction.descriptor.impacts, EditImpact::Runtime) &&
            !transaction.runtime_is_current) {
            std::string preview_error;
            const PreviewController::Snapshot preserved_preview = preview.capture();
            const bool playback_changed = !preview_playback_equal(
                preserved_preview.state,
                requested_after.preview_state);
            const runtime::AnimationStateSnapshot playback_snapshot =
                preview.animation_state()->capture_state();
            PreviewController next_preview;
            if (!next_preview.bind(
                    next_runtime,
                    requested_after.preview_state,
                    !playback_changed,
                    &preview_error) ||
                (!playback_changed &&
                 !next_preview.restore_playback(playback_snapshot, &preview_error))) {
                *load.project = transaction.before_history.project;
                preview.restore(transaction.before_preview, nullptr);
                active_transaction.reset();
                update_dirty();
                return SessionResult{
                    false,
                    make_error(
                        SessionErrorCode::PreviewUpdateFailed,
                        std::move(preview_error))};
            }
            if (!playback_changed) {
                next_preview.restore_transient_state(preserved_preview);
            }
            preview = std::move(next_preview);
            load.skeleton_data = std::move(next_runtime);
            ++runtime_revision;
            ++preview_revision;
        } else if (preview_changed) {
            if (transaction.pending_preview_state.has_value()) {
                const PreviewController::Snapshot current_preview = preview.capture();
                const bool playback_changed = !preview_playback_equal(
                    current_preview.state,
                    requested_after.preview_state);
                std::string preview_error;
                if (!preview.restore(
                        PreviewController::Snapshot{
                            requested_after.preview_state,
                            playback_changed
                                ? runtime::RootMotionDelta{}
                                : current_preview.root_motion_delta,
                            playback_changed
                                ? runtime::RootMotionDelta{}
                                : current_preview.root_motion_total,
                            playback_changed
                                ? std::vector<runtime::AnimationEvent>{}
                                : current_preview.events},
                        &preview_error)) {
                    *load.project = transaction.before_history.project;
                    preview.restore(transaction.before_preview, nullptr);
                    active_transaction.reset();
                    update_dirty();
                    return SessionResult{
                        false,
                        make_error(
                            SessionErrorCode::PreviewUpdateFailed,
                            std::move(preview_error))};
                }
            }
            ++preview_revision;
        }

        if (project_changed) {
            ++project_revision;
        }
        const HistorySnapshot after = capture_history();
        push_history(HistoryEntry{transaction.descriptor, transaction.before_history, after});
        active_transaction.reset();
        update_dirty();
        return SessionResult{true, std::nullopt};
    }

    SessionResult apply_history(const HistoryEntry& entry, bool use_before) {
        if (!loaded()) {
            return SessionResult{
                false,
                make_error(SessionErrorCode::NoProject, "No editor project is open.")};
        }
        const HistorySnapshot current = capture_history();
        const PreviewController::Snapshot current_preview = preview.capture();
        const HistorySnapshot& target = use_before ? entry.before : entry.after;
        if (histories_equal(current, target)) {
            return {};
        }

        const bool project_changed =
            current.serialized_project != target.serialized_project ||
            current.project.source_path != target.project.source_path;
        const bool preview_changed =
            !preview_states_equal(current.preview_state, target.preview_state);
        *load.project = target.project;

        std::shared_ptr<const runtime::SkeletonData> next_runtime = load.skeleton_data;
        if (project_changed) {
            const ProjectRuntimeResult runtime_result = build_project_runtime(
                *load.project,
                *load.base_skeleton_document);
            if (!runtime_result) {
                *load.project = current.project;
                return SessionResult{
                    false,
                    make_error(
                        SessionErrorCode::RuntimeBuildFailed,
                        "History restoration did not produce valid runtime data.",
                        runtime_result.error)};
            }
            next_runtime = runtime_result.skeleton_data;
        }

        if (project_changed && has_edit_impact(entry.descriptor.impacts, EditImpact::Runtime)) {
            const bool playback_changed = !preview_playback_equal(
                current_preview.state,
                target.preview_state);
            const runtime::AnimationStateSnapshot playback_snapshot =
                preview.animation_state()->capture_state();
            PreviewController next_preview;
            std::string preview_error;
            if (!next_preview.bind(
                    next_runtime,
                    target.preview_state,
                    !playback_changed,
                    &preview_error) ||
                (!playback_changed &&
                 !next_preview.restore_playback(playback_snapshot, &preview_error))) {
                *load.project = current.project;
                preview.restore(current_preview, nullptr);
                return SessionResult{
                    false,
                    make_error(
                        SessionErrorCode::PreviewUpdateFailed,
                        std::move(preview_error))};
            }
            if (!playback_changed) {
                next_preview.restore_transient_state(current_preview);
            }
            preview = std::move(next_preview);
            load.skeleton_data = std::move(next_runtime);
            ++runtime_revision;
            ++preview_revision;
        } else if (preview_changed) {
            const bool playback_changed = !preview_playback_equal(
                current_preview.state,
                target.preview_state);
            std::string preview_error;
            if (!preview.restore(
                    PreviewController::Snapshot{
                        target.preview_state,
                        playback_changed
                            ? runtime::RootMotionDelta{}
                            : current_preview.root_motion_delta,
                        playback_changed
                            ? runtime::RootMotionDelta{}
                            : current_preview.root_motion_total,
                        playback_changed
                            ? std::vector<runtime::AnimationEvent>{}
                            : current_preview.events},
                    &preview_error)) {
                *load.project = current.project;
                preview.restore(current_preview, nullptr);
                return SessionResult{
                    false,
                    make_error(
                        SessionErrorCode::PreviewUpdateFailed,
                        std::move(preview_error))};
            }
            ++preview_revision;
        }

        if (project_changed) {
            ++project_revision;
        }
        update_dirty();
        return SessionResult{true, std::nullopt};
    }
};

std::string SessionError::format() const {
    if (detail.has_value()) {
        return message.empty() ? detail->format() : message + "\n" + detail->format();
    }
    return message;
}

EditorSession::EditorSession() : impl_(std::make_unique<Impl>()) {}
EditorSession::~EditorSession() = default;
EditorSession::EditorSession(EditorSession&&) noexcept = default;
EditorSession& EditorSession::operator=(EditorSession&&) noexcept = default;

ProjectLoadResult EditorSession::open(const std::filesystem::path& path) {
    if (impl_->active_transaction.has_value()) {
        ProjectLoadResult result;
        result.error = make_session_load_error(
            path,
            "cannot open a project while an edit transaction is active");
        return result;
    }

    ProjectLoadResult attempted = load_project(path);
    if (!attempted) {
        return attempted;
    }

    PreviewState initial;
    initial.animation_name = attempted.project->editor_metadata.active_animation;
    initial.skin_names = attempted.project->editor_metadata.preview_skins;
    initial.slot_overrides.resize(attempted.skeleton_data->slots().size());
    PreviewController next_preview;
    std::string preview_error;
    if (!next_preview.bind(attempted.skeleton_data, std::move(initial), false, &preview_error)) {
        ProjectLoadResult failure;
        failure.error = make_session_load_error(path, std::move(preview_error));
        return failure;
    }

    impl_->load = attempted;
    impl_->load.project = std::make_shared<ProjectData>(*attempted.project);
    impl_->preview = std::move(next_preview);
    impl_->undo_entries.clear();
    impl_->redo_entries.clear();
    impl_->saved_serialized_project = serialize_project(*impl_->load.project);
    impl_->project_dirty = false;
    ++impl_->project_revision;
    ++impl_->runtime_revision;
    ++impl_->preview_revision;
    return attempted;
}

ProjectLoadResult EditorSession::reload() {
    if (!impl_->loaded()) {
        ProjectLoadResult result;
        result.error = make_session_load_error({}, "no editor project is open");
        return result;
    }
    if (impl_->active_transaction.has_value()) {
        ProjectLoadResult result;
        result.error = make_session_load_error(
            impl_->load.project->source_path,
            "cannot reload a project while an edit transaction is active");
        return result;
    }

    ProjectLoadResult attempted = load_project(impl_->load.project->source_path);
    if (!attempted) {
        return attempted;
    }

    const PreviewController::Snapshot previous_preview = impl_->preview.capture();
    const runtime::AnimationStateSnapshot playback_snapshot =
        impl_->preview.animation_state()->capture_state();
    PreviewState reloaded_state = impl_->preview.state();
    reloaded_state.skin_names = attempted.project->editor_metadata.preview_skins;
    reloaded_state.slot_overrides.assign(attempted.skeleton_data->slots().size(), std::nullopt);
    if (!reloaded_state.animation_name.empty() &&
        attempted.skeleton_data->find_animation(reloaded_state.animation_name) == nullptr) {
        reloaded_state.animation_name = attempted.project->editor_metadata.active_animation;
    }
    PreviewController next_preview;
    std::string preview_error;
    if (!next_preview.bind(
            attempted.skeleton_data,
            std::move(reloaded_state),
            false,
            &preview_error) ||
        !next_preview.restore_playback(playback_snapshot, &preview_error)) {
        ProjectLoadResult failure;
        failure.error = make_session_load_error(
            attempted.project->source_path,
            std::move(preview_error));
        return failure;
    }
    next_preview.restore_transient_state(previous_preview);

    impl_->load = attempted;
    impl_->load.project = std::make_shared<ProjectData>(*attempted.project);
    impl_->preview = std::move(next_preview);
    impl_->undo_entries.clear();
    impl_->redo_entries.clear();
    impl_->saved_serialized_project = serialize_project(*impl_->load.project);
    impl_->project_dirty = false;
    ++impl_->project_revision;
    ++impl_->runtime_revision;
    ++impl_->preview_revision;
    return attempted;
}

ProjectSaveResult EditorSession::save(const std::filesystem::path& path) {
    if (!impl_->loaded()) {
        return make_save_failure(path, "no editor project is open");
    }
    if (impl_->active_transaction.has_value()) {
        return make_save_failure(path, "cannot save while an edit transaction is active");
    }
    const std::filesystem::path output_path =
        path.empty() ? impl_->load.project->source_path : path;
    if (output_path.empty()) {
        return make_save_failure(output_path, "the project does not have a source path");
    }

    ProjectSaveResult result = save_project(*impl_->load.project, output_path);
    if (!result) {
        return result;
    }
    const bool source_changed = impl_->load.project->source_path != result.project->source_path;
    impl_->load.project = std::make_shared<ProjectData>(*result.project);
    if (source_changed) {
        const auto rebase_history = [&](std::vector<Impl::HistoryEntry>* entries) {
            for (Impl::HistoryEntry& entry : *entries) {
                entry.before.project.source_path = result.project->source_path;
                entry.after.project.source_path = result.project->source_path;
                entry.descriptor.allow_merge = false;
            }
        };
        rebase_history(&impl_->undo_entries);
        rebase_history(&impl_->redo_entries);
    } else {
        // Saving establishes a history boundary: subsequent edits with the
        // same merge key must remain independently undoable.
        for (Impl::HistoryEntry& entry : impl_->undo_entries) {
            entry.descriptor.allow_merge = false;
        }
        for (Impl::HistoryEntry& entry : impl_->redo_entries) {
            entry.descriptor.allow_merge = false;
        }
    }
    impl_->saved_serialized_project = serialize_project(*impl_->load.project);
    impl_->project_dirty = false;
    if (source_changed) {
        ++impl_->project_revision;
    }
    return result;
}

ProjectExportResult EditorSession::export_runtime(const ProjectExportOptions& options) const {
    if (!impl_->loaded()) {
        return make_export_failure(options.skeleton_output_path, "no editor project is open");
    }
    if (impl_->active_transaction.has_value()) {
        return make_export_failure(
            options.skeleton_output_path,
            "cannot export while an edit transaction is active");
    }
    return export_runtime_assets(
        *impl_->load.project,
        *impl_->load.base_skeleton_document,
        options);
}

bool EditorSession::has_project() const noexcept { return impl_->loaded(); }
const ProjectData* EditorSession::project() const noexcept { return impl_->load.project.get(); }
const runtime::json::Document* EditorSession::base_skeleton_document() const noexcept {
    return impl_->load.base_skeleton_document.get();
}
const runtime::SkeletonData* EditorSession::runtime_data() const noexcept {
    return impl_->load.skeleton_data.get();
}
const std::vector<std::shared_ptr<const runtime::AtlasData>>& EditorSession::atlas_data() const noexcept {
    return impl_->load.atlas_data;
}
const PreviewState& EditorSession::preview_state() const noexcept { return impl_->preview.state(); }
const runtime::Skeleton* EditorSession::preview_skeleton() const noexcept {
    return impl_->preview.skeleton();
}
const runtime::AnimationState* EditorSession::preview_animation_state() const noexcept {
    return impl_->preview.animation_state();
}
const std::vector<runtime::AnimationEvent>& EditorSession::preview_events() const noexcept {
    return impl_->preview.events();
}
runtime::RootMotionDelta EditorSession::preview_root_motion_delta() const noexcept {
    return impl_->preview.root_motion_delta();
}
runtime::RootMotionDelta EditorSession::preview_root_motion_total() const noexcept {
    return impl_->preview.root_motion_total();
}

bool EditorSession::select_animation(std::string_view animation_name, bool reset_time) {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    std::string error;
    if (!impl_->preview.select_animation(animation_name, reset_time, &error)) {
        return false;
    }
    ++impl_->preview_revision;
    return true;
}

bool EditorSession::select_setup_pose() {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    std::string error;
    if (!impl_->preview.select_setup_pose(&error)) {
        return false;
    }
    ++impl_->preview_revision;
    return true;
}

bool EditorSession::seek(double time_seconds) {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    std::string error;
    if (!impl_->preview.seek(time_seconds, &error)) {
        return false;
    }
    ++impl_->preview_revision;
    return true;
}

bool EditorSession::advance(double delta_seconds) {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    std::string error;
    if (!impl_->preview.advance(delta_seconds, &error)) {
        return false;
    }
    ++impl_->preview_revision;
    return true;
}

void EditorSession::set_playing(bool playing) noexcept {
    if (!impl_->loaded() || impl_->active_transaction.has_value() ||
        impl_->preview.state().playing == playing) {
        return;
    }
    impl_->preview.set_playing(playing);
    ++impl_->preview_revision;
}

bool EditorSession::set_loop(bool loop) {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    const bool changed = impl_->preview.state().loop != loop;
    std::string error;
    if (!impl_->preview.set_loop(loop, &error)) {
        return false;
    }
    if (changed) {
        ++impl_->preview_revision;
    }
    return true;
}

bool EditorSession::set_reverse(bool reverse) {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    const bool changed = impl_->preview.state().reverse != reverse;
    std::string error;
    if (!impl_->preview.set_reverse(reverse, &error)) {
        return false;
    }
    if (changed) {
        ++impl_->preview_revision;
    }
    return true;
}

bool EditorSession::set_queue(
    std::string_view animation_name,
    double delay_seconds,
    std::optional<double> mix_duration) {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    std::string error;
    if (!impl_->preview.set_queue(animation_name, delay_seconds, mix_duration, &error)) {
        return false;
    }
    ++impl_->preview_revision;
    return true;
}

bool EditorSession::clear_queue() {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    const bool changed = impl_->preview.state().queue_enabled;
    std::string error;
    if (!impl_->preview.clear_queue(&error)) {
        return false;
    }
    if (changed) {
        ++impl_->preview_revision;
    }
    return true;
}

SessionResult EditorSession::set_preview_skins(
    std::vector<std::string> skin_names,
    EditDescriptor descriptor) {
    EditTransaction transaction = begin_edit(std::move(descriptor));
    if (!transaction) {
        return SessionResult{false, transaction.error()};
    }
    if (!transaction.set_preview_skins(std::move(skin_names))) {
        const std::optional<SessionError> error = transaction.error();
        transaction.cancel();
        return SessionResult{false, error};
    }
    return transaction.commit();
}

SessionResult EditorSession::set_preview_attachment(
    std::size_t slot_index,
    std::optional<std::size_t> skin_index,
    std::string attachment_name,
    EditDescriptor descriptor) {
    if (descriptor.merge_key == "preview-attachment") {
        descriptor.merge_key += ":" + std::to_string(slot_index);
    }
    EditTransaction transaction = begin_edit(std::move(descriptor));
    if (!transaction) {
        return SessionResult{false, transaction.error()};
    }
    if (!transaction.set_preview_attachment(
            slot_index,
            skin_index,
            std::move(attachment_name))) {
        const std::optional<SessionError> error = transaction.error();
        transaction.cancel();
        return SessionResult{false, error};
    }
    return transaction.commit();
}

SessionResult EditorSession::reset_preview_attachment(
    std::size_t slot_index,
    EditDescriptor descriptor) {
    if (descriptor.merge_key == "preview-attachment") {
        descriptor.merge_key += ":" + std::to_string(slot_index);
    }
    EditTransaction transaction = begin_edit(std::move(descriptor));
    if (!transaction) {
        return SessionResult{false, transaction.error()};
    }
    if (!transaction.reset_preview_attachment(slot_index)) {
        const std::optional<SessionError> error = transaction.error();
        transaction.cancel();
        return SessionResult{false, error};
    }
    return transaction.commit();
}

SessionResult EditorSession::edit_animation_catalog(
    AnimationCatalogEdit edit,
    EditDescriptor descriptor) {
    if (!impl_->loaded()) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::NoProject,
                "No editor project is open.")};
    }

    EditTransaction transaction = begin_edit(std::move(descriptor));
    if (!transaction) {
        return SessionResult{false, transaction.error()};
    }

    AuthoringResult authoring_result;
    switch (edit.kind) {
    case AnimationCatalogEditKind::Create:
        authoring_result = create_animation(
            transaction.project(),
            *impl_->load.base_skeleton_document,
            edit.destination_animation);
        break;
    case AnimationCatalogEditKind::Duplicate:
        authoring_result = duplicate_animation(
            transaction.project(),
            *impl_->load.base_skeleton_document,
            edit.source_animation,
            edit.destination_animation);
        break;
    case AnimationCatalogEditKind::Rename:
        authoring_result = rename_animation(
            transaction.project(),
            *impl_->load.base_skeleton_document,
            edit.source_animation,
            edit.destination_animation);
        break;
    case AnimationCatalogEditKind::Delete:
        authoring_result = delete_animation(
            transaction.project(),
            *impl_->load.base_skeleton_document,
            edit.source_animation);
        break;
    }

    if (!authoring_result) {
        const std::string message = authoring_result.error;
        transaction.cancel();
        return SessionResult{
            false,
            Impl::make_error(SessionErrorCode::InvalidTransaction, message)};
    }
    if (!authoring_result.changed) {
        transaction.cancel();
        return {};
    }

    PreviewState desired_preview = impl_->preview.state();
    const auto select_animation = [&](std::string animation_name) {
        if (desired_preview.animation_name == animation_name) {
            return;
        }
        desired_preview.animation_name = std::move(animation_name);
        desired_preview.time_seconds = 0.0;
        desired_preview.playing = false;
    };
    const auto remove_queue = [&]() {
        desired_preview.queue_enabled = false;
        desired_preview.queued_animation_name.clear();
    };

    switch (edit.kind) {
    case AnimationCatalogEditKind::Create:
    case AnimationCatalogEditKind::Duplicate:
        select_animation(edit.destination_animation);
        break;
    case AnimationCatalogEditKind::Rename:
        if (desired_preview.animation_name == edit.source_animation) {
            // Rename is identity-preserving: keep playback time and running
            // state while remapping the catalog reference.
            desired_preview.animation_name = edit.destination_animation;
        }
        if (desired_preview.queued_animation_name == edit.source_animation) {
            desired_preview.queued_animation_name = edit.destination_animation;
        }
        if (desired_preview.queue_enabled &&
            desired_preview.queued_animation_name == desired_preview.animation_name) {
            remove_queue();
        }
        break;
    case AnimationCatalogEditKind::Delete:
        if (desired_preview.animation_name == edit.source_animation) {
            std::string replacement = transaction.project()->editor_metadata.active_animation;
            if (replacement.empty() || replacement == edit.source_animation) {
                const auto replacement_it = std::find_if(
                    impl_->load.skeleton_data->animations().begin(),
                    impl_->load.skeleton_data->animations().end(),
                    [&](const runtime::AnimationData& animation) {
                        return animation.name != edit.source_animation;
                    });
                replacement = replacement_it != impl_->load.skeleton_data->animations().end()
                    ? replacement_it->name
                    : std::string{};
            }
            select_animation(std::move(replacement));
        }
        if (desired_preview.queued_animation_name == edit.source_animation ||
            (desired_preview.queue_enabled &&
             desired_preview.queued_animation_name == desired_preview.animation_name)) {
            remove_queue();
        }
        break;
    }

    impl_->active_transaction->pending_preview_state = std::move(desired_preview);
    return transaction.commit();
}

EditorSession::EditTransaction EditorSession::begin_edit(EditDescriptor descriptor) {
    if (!impl_->loaded()) {
        return EditTransaction(
            nullptr,
            0U,
            Impl::make_error(SessionErrorCode::NoProject, "No editor project is open."));
    }
    if (impl_->active_transaction.has_value()) {
        return EditTransaction(
            nullptr,
            0U,
            Impl::make_error(
                SessionErrorCode::TransactionAlreadyActive,
                "EditorSession edit transactions cannot be nested."));
    }
    if (descriptor.label.empty()) {
        descriptor.label = "Edit project";
    }
    const std::uint64_t id = impl_->next_transaction_id++;
    impl_->active_transaction = Impl::ActiveTransaction{
        id,
        std::move(descriptor),
        impl_->capture_history(),
        impl_->preview.capture(),
        impl_->load.skeleton_data,
        impl_->preview.animation_state()->capture_state(),
        std::nullopt,
        false,
        false};
    return EditTransaction(impl_.get(), id);
}

bool EditorSession::transaction_active() const noexcept {
    return impl_->active_transaction.has_value();
}

SessionResult EditorSession::undo() {
    if (impl_->active_transaction.has_value()) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::TransactionAlreadyActive,
                "Cannot undo while an edit transaction is active.")};
    }
    if (impl_->undo_entries.empty()) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::HistoryEmpty,
                "There is no editor action to undo.")};
    }
    Impl::HistoryEntry entry = impl_->undo_entries.back();
    const SessionResult result = impl_->apply_history(entry, true);
    if (!result) {
        return result;
    }
    impl_->undo_entries.pop_back();
    impl_->redo_entries.push_back(std::move(entry));
    return result;
}

SessionResult EditorSession::redo() {
    if (impl_->active_transaction.has_value()) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::TransactionAlreadyActive,
                "Cannot redo while an edit transaction is active.")};
    }
    if (impl_->redo_entries.empty()) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::HistoryEmpty,
                "There is no editor action to redo.")};
    }
    Impl::HistoryEntry entry = impl_->redo_entries.back();
    const SessionResult result = impl_->apply_history(entry, false);
    if (!result) {
        return result;
    }
    impl_->redo_entries.pop_back();
    impl_->undo_entries.push_back(std::move(entry));
    return result;
}

bool EditorSession::can_undo() const noexcept { return !impl_->undo_entries.empty(); }
bool EditorSession::can_redo() const noexcept { return !impl_->redo_entries.empty(); }
std::size_t EditorSession::undo_count() const noexcept { return impl_->undo_entries.size(); }
std::size_t EditorSession::redo_count() const noexcept { return impl_->redo_entries.size(); }
std::string_view EditorSession::undo_label() const noexcept {
    return impl_->undo_entries.empty()
        ? std::string_view{}
        : std::string_view(impl_->undo_entries.back().descriptor.label);
}
std::string_view EditorSession::redo_label() const noexcept {
    return impl_->redo_entries.empty()
        ? std::string_view{}
        : std::string_view(impl_->redo_entries.back().descriptor.label);
}
void EditorSession::clear_history() noexcept {
    if (impl_->active_transaction.has_value()) {
        return;
    }
    impl_->undo_entries.clear();
    impl_->redo_entries.clear();
}

bool EditorSession::dirty() const noexcept { return impl_->project_dirty; }
std::uint64_t EditorSession::project_revision() const noexcept {
    return impl_->project_revision;
}
std::uint64_t EditorSession::runtime_revision() const noexcept {
    return impl_->runtime_revision;
}
std::uint64_t EditorSession::preview_revision() const noexcept {
    return impl_->preview_revision;
}

ProjectLoadResult& EditorSession::mutable_load_result() { return impl_->load; }
runtime::Skeleton* EditorSession::mutable_preview_skeleton() noexcept {
    return impl_->preview.mutable_skeleton();
}
runtime::AnimationState* EditorSession::mutable_preview_animation_state() noexcept {
    return impl_->preview.mutable_animation_state();
}

bool EditorSession::sync_preview_state(const PreviewState& state) {
    if (!impl_->loaded() || impl_->active_transaction.has_value()) {
        return false;
    }
    const PreviewController::Snapshot current = impl_->preview.capture();
    if (preview_states_equal(current.state, state)) {
        return true;
    }
    const bool restored = impl_->preview.restore(
        PreviewController::Snapshot{
            state,
            current.root_motion_delta,
            current.root_motion_total,
            current.events},
        nullptr);
    if (restored && !preview_states_equal(current.state, impl_->preview.state())) {
        ++impl_->preview_revision;
    }
    return restored;
}

SessionResult EditorSession::commit_external_edit(
    const ProjectData& before_project,
    const PreviewState& before_preview,
    EditDescriptor descriptor,
    bool runtime_is_current) {
    if (!impl_->loaded()) {
        return SessionResult{
            false,
            Impl::make_error(SessionErrorCode::NoProject, "No editor project is open.")};
    }
    if (impl_->active_transaction.has_value()) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::TransactionAlreadyActive,
                "Cannot adopt an external edit while a transaction is active.")};
    }
    if (descriptor.label.empty()) {
        descriptor.label = "Edit project";
    }

    Impl::HistorySnapshot before_history;
    before_history.project = before_project;
    before_history.serialized_project = serialize_project(before_project);
    before_history.preview_state = before_preview;
    const PreviewController::Snapshot current_preview = impl_->preview.capture();
    const std::uint64_t id = impl_->next_transaction_id++;
    impl_->active_transaction = Impl::ActiveTransaction{
        id,
        std::move(descriptor),
        std::move(before_history),
        PreviewController::Snapshot{
            before_preview,
            current_preview.root_motion_delta,
            current_preview.root_motion_total,
            current_preview.events},
        impl_->load.skeleton_data,
        impl_->preview.animation_state()->capture_state(),
        std::nullopt,
        runtime_is_current,
        false};
    return impl_->commit(id);
}

SessionResult EditorSession::rebuild_runtime_without_history() {
    if (!impl_->loaded()) {
        return SessionResult{
            false,
            Impl::make_error(SessionErrorCode::NoProject, "No editor project is open.")};
    }
    if (impl_->active_transaction.has_value()) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::TransactionAlreadyActive,
                "Cannot rebuild runtime while an edit transaction is active.")};
    }

    const ProjectRuntimeResult runtime_result = build_project_runtime(
        *impl_->load.project,
        *impl_->load.base_skeleton_document);
    if (!runtime_result) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::RuntimeBuildFailed,
                "The project did not produce valid runtime data.",
                runtime_result.error)};
    }

    const PreviewController::Snapshot previous_preview = impl_->preview.capture();
    const runtime::AnimationStateSnapshot playback_snapshot =
        impl_->preview.animation_state()->capture_state();
    PreviewController next_preview;
    std::string preview_error;
    if (!next_preview.bind(
            runtime_result.skeleton_data,
            impl_->preview.state(),
            true,
            &preview_error) ||
        !next_preview.restore_playback(playback_snapshot, &preview_error)) {
        return SessionResult{
            false,
            Impl::make_error(
                SessionErrorCode::PreviewUpdateFailed,
                std::move(preview_error))};
    }
    next_preview.restore_transient_state(previous_preview);

    impl_->load.skeleton_data = runtime_result.skeleton_data;
    impl_->preview = std::move(next_preview);
    impl_->update_dirty();
    ++impl_->runtime_revision;
    ++impl_->preview_revision;
    return SessionResult{true, std::nullopt};
}

EditorSession::EditTransaction::EditTransaction() noexcept = default;
EditorSession::EditTransaction::EditTransaction(
    Impl* impl,
    std::uint64_t transaction_id,
    std::optional<SessionError> error) noexcept
    : impl_(impl), transaction_id_(transaction_id), error_(std::move(error)) {}

EditorSession::EditTransaction::~EditTransaction() { cancel(); }

EditorSession::EditTransaction::EditTransaction(EditTransaction&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr)),
      transaction_id_(std::exchange(other.transaction_id_, 0U)),
      error_(std::move(other.error_)) {}

EditorSession::EditTransaction& EditorSession::EditTransaction::operator=(
    EditTransaction&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    cancel();
    impl_ = std::exchange(other.impl_, nullptr);
    transaction_id_ = std::exchange(other.transaction_id_, 0U);
    error_ = std::move(other.error_);
    return *this;
}

EditorSession::EditTransaction::operator bool() const noexcept {
    return impl_ != nullptr && transaction_id_ != 0U && !error_.has_value();
}

const std::optional<SessionError>& EditorSession::EditTransaction::error() const noexcept {
    return error_;
}

ProjectData* EditorSession::EditTransaction::project() noexcept {
    if (!*this || !impl_->active_transaction.has_value() ||
        impl_->active_transaction->id != transaction_id_) {
        return nullptr;
    }
    return impl_->load.project.get();
}

const ProjectData* EditorSession::EditTransaction::project() const noexcept {
    return const_cast<EditTransaction*>(this)->project();
}

bool EditorSession::EditTransaction::set_preview_skins(
    std::vector<std::string> skin_names) {
    if (!*this) {
        return false;
    }
    SessionError error;
    if (!impl_->set_preview_skins(std::move(skin_names), &error)) {
        error_ = std::move(error);
        return false;
    }
    return true;
}

bool EditorSession::EditTransaction::set_preview_attachment(
    std::size_t slot_index,
    std::optional<std::size_t> skin_index,
    std::string attachment_name) {
    if (!*this) {
        return false;
    }
    SessionError error;
    if (!impl_->set_preview_attachment(
            slot_index,
            skin_index,
            std::move(attachment_name),
            &error)) {
        error_ = std::move(error);
        return false;
    }
    return true;
}

bool EditorSession::EditTransaction::reset_preview_attachment(std::size_t slot_index) {
    if (!*this) {
        return false;
    }
    SessionError error;
    if (!impl_->reset_preview_attachment(slot_index, &error)) {
        error_ = std::move(error);
        return false;
    }
    return true;
}

SessionResult EditorSession::EditTransaction::refresh_runtime() {
    if (!*this) {
        return SessionResult{
            false,
            error_.has_value()
                ? error_
                : std::optional<SessionError>(Impl::make_error(
                      SessionErrorCode::InvalidTransaction,
                      "The edit transaction is not active."))};
    }
    SessionResult result = impl_->refresh_runtime(transaction_id_);
    if (!result) {
        error_ = result.error;
    }
    return result;
}

SessionResult EditorSession::EditTransaction::commit() {
    if (!*this) {
        SessionResult result{
            false,
            error_.has_value()
                ? error_
                : std::optional<SessionError>(Impl::make_error(
                      SessionErrorCode::InvalidTransaction,
                      "The edit transaction is not active."))};
        if (impl_ != nullptr && transaction_id_ != 0U &&
            impl_->active_transaction.has_value() &&
            impl_->active_transaction->id == transaction_id_) {
            impl_->restore_active_transaction();
        }
        impl_ = nullptr;
        transaction_id_ = 0U;
        return result;
    }
    SessionResult result = impl_->commit(transaction_id_);
    impl_ = nullptr;
    transaction_id_ = 0U;
    if (!result) {
        error_ = result.error;
    }
    return result;
}

void EditorSession::EditTransaction::cancel() noexcept {
    if (impl_ != nullptr && transaction_id_ != 0U &&
        impl_->active_transaction.has_value() &&
        impl_->active_transaction->id == transaction_id_) {
        impl_->restore_active_transaction();
    }
    impl_ = nullptr;
    transaction_id_ = 0U;
}

} // namespace marrow::editor
