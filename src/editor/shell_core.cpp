#include "shell_types.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "marrow/editor/project.hpp"

namespace marrow::editor::shell {

// EditAction implementations
EditAction::EditAction(EditActionKind kind, std::string label, std::string group, bool allow_merge)
    : kind_(kind), label_(std::move(label)), group_(std::move(group)), allow_merge_(allow_merge) {}

EditActionKind EditAction::kind() const { return kind_; }
const std::string& EditAction::label() const { return label_; }
const std::string& EditAction::group() const { return group_; }
bool EditAction::allow_merge() const { return allow_merge_; }

SnapshotEditAction::SnapshotEditAction(
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    EditorHistorySnapshot before,
    EditorHistorySnapshot after)
    : EditAction(kind, std::move(label), std::move(group), allow_merge),
      before_(std::move(before)),
      after_(std::move(after)) {}

bool SnapshotEditAction::undo(ShellState* state) const {
    return apply_history_snapshot(state, before_);
}

bool SnapshotEditAction::redo(ShellState* state) const {
    return apply_history_snapshot(state, after_);
}

bool SnapshotEditAction::merge_from(const EditAction& action) {
    if (!allow_merge_ || group_.empty() || !action.allow_merge() ||
        action.kind() != kind_ || action.group() != group_) {
        return false;
    }

    const auto* snapshot_action = dynamic_cast<const SnapshotEditAction*>(&action);
    if (snapshot_action == nullptr) {
        return false;
    }

    after_ = snapshot_action->after_;
    label_ = snapshot_action->label();
    return true;
}

// UndoStack implementation
bool UndoStack::can_undo() const {
    return !undo_actions_.empty();
}

bool UndoStack::can_redo() const {
    return !redo_actions_.empty();
}

std::size_t UndoStack::undo_count() const {
    return undo_actions_.size();
}

std::size_t UndoStack::redo_count() const {
    return redo_actions_.size();
}

void UndoStack::clear() {
    undo_actions_.clear();
    redo_actions_.clear();
}

const EditAction* UndoStack::peek_undo() const {
    return undo_actions_.empty() ? nullptr : undo_actions_.back().get();
}

const EditAction* UndoStack::peek_redo() const {
    return redo_actions_.empty() ? nullptr : redo_actions_.back().get();
}

void UndoStack::push(std::unique_ptr<EditAction> action) {
    if (!action) {
        return;
    }

    redo_actions_.clear();
    if (!undo_actions_.empty() && undo_actions_.back()->merge_from(*action)) {
        return;
    }

    if (undo_actions_.size() >= kMaxDepth) {
        undo_actions_.erase(undo_actions_.begin());
    }
    undo_actions_.push_back(std::move(action));
}

bool UndoStack::undo(ShellState* state, std::string* label_out) {
    if (undo_actions_.empty()) {
        return false;
    }

    std::unique_ptr<EditAction> action = std::move(undo_actions_.back());
    undo_actions_.pop_back();
    if (!action->undo(state)) {
        undo_actions_.push_back(std::move(action));
        return false;
    }

    if (label_out != nullptr) {
        *label_out = action->label();
    }
    redo_actions_.push_back(std::move(action));
    return true;
}

bool UndoStack::redo(ShellState* state, std::string* label_out) {
    if (redo_actions_.empty()) {
        return false;
    }

    std::unique_ptr<EditAction> action = std::move(redo_actions_.back());
    redo_actions_.pop_back();
    if (!action->redo(state)) {
        redo_actions_.push_back(std::move(action));
        return false;
    }

    if (label_out != nullptr) {
        *label_out = action->label();
    }
    undo_actions_.push_back(std::move(action));
    return true;
}

// Shell Core Helpers
std::optional<std::string_view> selected_bone_name(const ShellState& state) {
    if (!state.load_result || !state.selected_bone_index.has_value()) {
        return std::nullopt;
    }

    const auto& bones = state.load_result.skeleton_data->bones();
    if (*state.selected_bone_index >= bones.size()) {
        return std::nullopt;
    }

    return bones[*state.selected_bone_index].name;
}

std::vector<std::string> normalize_preview_skin_names(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names) {
    std::vector<std::string> result;
    for (const auto& skin_name : preview_skin_names) {
        if (skeleton.find_skin_index(skin_name).has_value()) {
            result.push_back(skin_name);
        }
    }
    return result;
}

const marrow::runtime::AnimationData* selected_animation(const ShellState& state) {
    if (!state.load_result || state.selected_animation_name.empty()) {
        return nullptr;
    }

    return state.load_result.skeleton_data->find_animation(state.selected_animation_name);
}

double selected_animation_duration(const ShellState& state) {
    const marrow::runtime::AnimationData* animation = selected_animation(state);
    return animation != nullptr ? std::max(animation->duration(), 0.0) : 0.0;
}

const marrow::runtime::AnimationData* queued_preview_animation(const ShellState& state) {
    if (!state.load_result || state.preview_queued_animation_name.empty()) {
        return nullptr;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(state.preview_queued_animation_name);
    if (animation == nullptr || animation->name == state.selected_animation_name) {
        return nullptr;
    }

    return animation;
}

std::string default_queued_preview_animation_name(const ShellState& state) {
    if (!state.load_result) {
        return {};
    }

    for (const auto& animation : state.load_result.skeleton_data->animations()) {
        if (animation.name != state.selected_animation_name) {
            return animation.name;
        }
    }

    return {};
}

void normalize_state_preview_settings(ShellState* state) {
    if (state == nullptr || !state->load_result) {
        return;
    }

    if (state->preview_custom_mix_duration < 0.0) {
        state->preview_custom_mix_duration = 0.0;
    }
    if (state->preview_queue_delay < 0.0) {
        state->preview_queue_delay = 0.0;
    }

    const auto* queued_animation = queued_preview_animation(*state);
    if (queued_animation == nullptr) {
        state->preview_queued_animation_name = default_queued_preview_animation_name(*state);
        if (state->preview_queued_animation_name.empty()) {
            state->preview_queue_enabled = false;
        }
    }
}

double timeline_preview_duration(const ShellState& state) {
    const double primary_duration = selected_animation_duration(state);
    if (!state.preview_queue_enabled) {
        return primary_duration;
    }

    const auto* queued_animation = queued_preview_animation(state);
    if (queued_animation == nullptr) {
        return primary_duration;
    }

    return std::max(0.0, primary_duration) +
        std::max(0.0, state.preview_queue_delay) +
        std::max(0.0, queued_animation->duration());
}

static bool attachment_selection_equal(
    const std::optional<AttachmentSelection>& left,
    const std::optional<AttachmentSelection>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }

    return left->slot_index == right->slot_index &&
        left->skin_index == right->skin_index &&
        left->attachment_name == right->attachment_name;
}

EditorHistorySnapshot capture_history_snapshot(
    const ShellState& state,
    bool include_serialized_project) {
    EditorHistorySnapshot snapshot;
    if (state.load_result.project != nullptr) {
        snapshot.project = *state.load_result.project;
        if (include_serialized_project) {
            snapshot.serialized_project =
                marrow::editor::serialize_project(*state.load_result.project);
        }
    }
    snapshot.preview_skin_names = state.preview_skin_names;
    snapshot.preview_slot_overrides = state.preview_slot_overrides;
    return snapshot;
}

bool history_snapshots_equal(
    const EditorHistorySnapshot& left,
    const EditorHistorySnapshot& right) {
    if (left.serialized_project != right.serialized_project ||
        left.preview_skin_names != right.preview_skin_names ||
        left.preview_slot_overrides.size() != right.preview_slot_overrides.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.preview_slot_overrides.size(); ++index) {
        if (!attachment_selection_equal(
                left.preview_slot_overrides[index],
                right.preview_slot_overrides[index])) {
            return false;
        }
    }

    return true;
}

void assign_history_snapshot(
    ShellState* state,
    const EditorHistorySnapshot& snapshot) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return;
    }

    *state->load_result.project = snapshot.project;
    state->viewport.onion_skin = snapshot.project.editor_metadata.viewport.onion_skin;
    state->preview_skin_names = snapshot.preview_skin_names;
    state->preview_slot_overrides = snapshot.preview_slot_overrides;
}

void restore_history_snapshot(
    ShellState* state,
    const EditorHistorySnapshot& snapshot) {
    if (state == nullptr) {
        return;
    }

    assign_history_snapshot(state, snapshot);
    rebuild_project_runtime(state);
}

bool apply_history_snapshot(ShellState* state, const EditorHistorySnapshot& snapshot) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const std::string serialized =
        marrow::editor::serialize_project(*state->load_result.project);
    if (serialized == snapshot.serialized_project &&
        state->preview_skin_names == snapshot.preview_skin_names) {
        
        bool overrides_equal = true;
        if (state->preview_slot_overrides.size() != snapshot.preview_slot_overrides.size()) {
            overrides_equal = false;
        } else {
            for (std::size_t i = 0; i < state->preview_slot_overrides.size(); ++i) {
                if (!attachment_selection_equal(state->preview_slot_overrides[i], snapshot.preview_slot_overrides[i])) {
                    overrides_equal = false;
                    break;
                }
            }
        }
        
        if (overrides_equal) {
            return true;
        }
    }

    restore_history_snapshot(state, snapshot);
    return true;
}

void update_project_dirty_state(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        state->project_dirty = false;
        return;
    }

    state->project_dirty =
        marrow::editor::serialize_project(*state->load_result.project) !=
        state->saved_project_snapshot;
}

std::unique_ptr<EditAction> make_edit_action(
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    const EditorHistorySnapshot& before,
    const EditorHistorySnapshot& after) {
    switch (kind) {
    case EditActionKind::MoveBone:
        return std::make_unique<MoveBoneAction>(
            kind, std::move(label), std::move(group), allow_merge, before, after);
    case EditActionKind::AddKeyframe:
        return std::make_unique<AddKeyframeAction>(
            kind, std::move(label), std::move(group), allow_merge, before, after);
    case EditActionKind::RemoveKeyframe:
        return std::make_unique<RemoveKeyframeAction>(
            kind, std::move(label), std::move(group), allow_merge, before, after);
    case EditActionKind::EditProperty:
        return std::make_unique<EditPropertyAction>(
            kind, std::move(label), std::move(group), allow_merge, before, after);
    }

    return std::make_unique<SnapshotEditAction>(
        kind, std::move(label), std::move(group), allow_merge, before, after);
}

bool record_action_from_snapshots(
    ShellState* state,
    const EditorHistorySnapshot& before,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge) {
    const EditorHistorySnapshot after = capture_history_snapshot(*state, true);
    if (history_snapshots_equal(before, after)) {
        return false;
    }

    state->command_stack.push(
        make_edit_action(kind, label, group, allow_merge, before, after));
    update_project_dirty_state(state);
    state->error_message.clear();
    state->status_message = std::move(label);
    return true;
}

bool rebuild_project_runtime(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr ||
        state->load_result.base_skeleton_document == nullptr) {
        return false;
    }

    std::optional<marrow::runtime::AnimationStateSnapshot> playback_snapshot;
    if (state->animation_state != nullptr) {
        playback_snapshot = state->animation_state->capture_state();
    }

    const auto runtime_result = marrow::editor::build_project_runtime(
        *state->load_result.project,
        *state->load_result.base_skeleton_document);
    if (!runtime_result) {
        state->error_message = runtime_result.error->format();
        return false;
    }

    state->load_result.skeleton_data = runtime_result.skeleton_data;
    state->preview_skeleton =
        marrow::allocate_unique<marrow::runtime::Skeleton>(state->load_result.skeleton_data);
    state->animation_state =
        marrow::allocate_unique<marrow::runtime::AnimationState>(
            state->load_result.skeleton_data);
    state->preview_skin_names = normalize_preview_skin_names(
        *state->load_result.skeleton_data,
        state->preview_skin_names);
    state->preview_slot_overrides.resize(state->load_result.skeleton_data->slots().size());

    if (!state->selected_animation_name.empty() &&
        state->load_result.skeleton_data->find_animation(state->selected_animation_name) == nullptr) {
        state->selected_animation_name.clear();
    }
    
    return true;
}

bool reload_project(ShellState* state) {
    if (state == nullptr) return false;

    std::optional<std::string> previous_selection_name;
    if (const auto selection_name = selected_bone_name(*state)) {
        previous_selection_name = std::string(*selection_name);
    }
    
    std::optional<std::string> previous_slot_name;
    if (state->load_result && state->selected_slot_index.has_value() &&
        *state->selected_slot_index < state->load_result.skeleton_data->slots().size()) {
        previous_slot_name =
            state->load_result.skeleton_data->slots()[*state->selected_slot_index].name;
    }
    const std::string previous_animation_name = state->selected_animation_name;
    const double previous_timeline_time = state->timeline_time_seconds;
    const bool previous_timeline_loop = state->timeline_loop;
    const bool previous_timeline_playing = state->timeline_playing;

    state->load_result = marrow::editor::load_project(state->project_path);
    state->preview_skeleton.reset();
    state->animation_state.reset();
    state->selected_bone_index.reset();
    state->selected_slot_index.reset();
    state->selected_attachment.reset();
    state->selected_timeline_track_id.reset();
    state->selected_constraint.reset();
    state->preview_skin_names.clear();
    state->preview_slot_overrides.clear();
    state->selected_animation_name.clear();
    state->timeline_time_seconds = 0.0;
    state->timeline_loop = previous_timeline_loop;
    state->timeline_playing = false;
    state->command_stack.clear();
    state->pending_edit_action.reset();
    
    state->project_dirty = false;
    state->saved_project_snapshot.clear();
    state->error_message.clear();

    if (!state->load_result) {
        state->status_message = "Project load failed";
        if (state->load_result.error.has_value()) {
            state->error_message = state->load_result.error->format();
        } else {
            state->error_message = "Unknown project load failure.";
        }
        return false;
    }

    state->viewport = state->load_result.project->editor_metadata.viewport;
    state->saved_project_snapshot =
        marrow::editor::serialize_project(*state->load_result.project);
    state->preview_skeleton =
        marrow::allocate_unique<marrow::runtime::Skeleton>(state->load_result.skeleton_data);
    state->animation_state =
        marrow::allocate_unique<marrow::runtime::AnimationState>(
            state->load_result.skeleton_data);
    state->preview_skin_names = normalize_preview_skin_names(
        *state->load_result.skeleton_data,
        state->load_result.project->editor_metadata.preview_skins);
    state->preview_slot_overrides.resize(state->load_result.skeleton_data->slots().size());

    const auto& animations = state->load_result.skeleton_data->animations();
    if (!previous_animation_name.empty() &&
        state->load_result.skeleton_data->find_animation(previous_animation_name) != nullptr) {
        state->selected_animation_name = previous_animation_name;
    } else if (!state->load_result.project->editor_metadata.active_animation.empty() &&
               state->load_result.skeleton_data->find_animation(
                   state->load_result.project->editor_metadata.active_animation) != nullptr) {
        state->selected_animation_name = state->load_result.project->editor_metadata.active_animation;
    } else if (!animations.empty()) {
        state->selected_animation_name = animations.front().name;
    }
    normalize_state_preview_settings(state);
    if (!state->selected_animation_name.empty()) {
        state->timeline_time_seconds = std::clamp(
            previous_timeline_time,
            0.0,
            timeline_preview_duration(*state));
        state->timeline_playing = previous_timeline_playing;
    }

    return true;
}

bool save_project_file(ShellState* state, bool update_status_message) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const auto save_result =
        marrow::editor::save_project(*state->load_result.project, state->project_path);
    if (!save_result) {
        state->error_message = save_result.error->format();
        state->status_message = "Project save failed";
        return false;
    }

    state->load_result.project = save_result.project;
    state->saved_project_snapshot =
        marrow::editor::serialize_project(*state->load_result.project);
    state->project_dirty = false;
    state->error_message.clear();
    if (update_status_message) {
        state->status_message = "Saved project to " + state->project_path.string();
    }
    return true;
}

bool export_runtime_assets_file(ShellState* state, bool update_status_message) {
    if (!state->load_result || state->load_result.project == nullptr ||
        state->load_result.base_skeleton_document == nullptr) {
        return false;
    }

    marrow::editor::ProjectExportOptions export_options;
    if (state->export_binary_output) {
        export_options.binary_output_path =
            state->load_result.project->resolved_export_binary_path();
    }

    const auto export_result = marrow::editor::export_runtime_assets(
        *state->load_result.project,
        *state->load_result.base_skeleton_document,
        export_options);
    if (!export_result) {
        state->error_message = export_result.error->format();
        state->status_message = "Runtime export failed";
        return false;
    }

    state->error_message.clear();
    if (update_status_message) {
        std::string message = "Exported runtime assets to " + export_result.path.string();
        if (!export_result.atlas_paths.empty()) {
            message += " with " + std::to_string(export_result.atlas_paths.size()) +
                " atlas file";
            if (export_result.atlas_paths.size() != 1U) {
                message += "s";
            }
        }
        if (export_result.binary_path.has_value()) {
            message += " and " + export_result.binary_path->filename().string();
        }
        state->status_message = std::move(message);
    }
    return true;
}

} // namespace marrow::editor::shell
