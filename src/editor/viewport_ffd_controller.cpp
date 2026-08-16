#include "viewport_ffd_controller.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "viewport_interaction_kernel.hpp"
#include "marrow/editor/project.hpp"

namespace marrow::editor::shell::viewport_ffd {
namespace {

namespace kernel = marrow::editor::viewport_interaction_kernel;

constexpr double kOffsetEqualityEpsilon = 1e-12;
constexpr double kWorldVerificationEpsilon = 1e-4;

struct ResolvedFfdTarget {
    std::size_t slot_index{0U};
    std::optional<std::size_t> display_skin_index;
    std::string display_attachment_name;
    std::string deform_attachment_name;
    const marrow::runtime::AttachmentData* display_attachment{nullptr};
    const marrow::runtime::AttachmentData* deform_attachment{nullptr};
    std::vector<ViewportWorldPoint> vertex_world_positions;
};

ViewportFfdSelectionScope selection_scope(const ResolvedFfdTarget& target) {
    return ViewportFfdSelectionScope{
        target.slot_index,
        target.display_skin_index,
        target.display_attachment_name,
        target.deform_attachment_name,
        target.vertex_world_positions.size()};
}

ViewportFfdSelectionScope selection_scope(const ViewportFfdOverlay& overlay) {
    return ViewportFfdSelectionScope{
        overlay.slot_index,
        overlay.display_skin_index,
        overlay.display_attachment_name,
        overlay.deform_attachment_name,
        overlay.vertex_world_positions.size()};
}

bool scopes_equal(
    const ViewportFfdSelectionScope& left,
    const ViewportFfdSelectionScope& right) {
    return left.slot_index == right.slot_index &&
        left.display_skin_index == right.display_skin_index &&
        left.display_attachment_name == right.display_attachment_name &&
        left.deform_attachment_name == right.deform_attachment_name &&
        left.vertex_count == right.vertex_count;
}

bool valid_vertex_indices(
    const std::vector<std::size_t>& indices,
    std::size_t vertex_count) {
    if (indices.empty()) {
        return false;
    }
    std::optional<std::size_t> previous;
    for (const std::size_t vertex_index : indices) {
        if (vertex_index >= vertex_count ||
            (previous.has_value() && vertex_index <= *previous)) {
            return false;
        }
        previous = vertex_index;
    }
    return true;
}

bool ffd_selection_equal(
    const std::optional<ViewportFfdSelection>& left,
    const std::optional<ViewportFfdSelection>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
        (scopes_equal(left->scope, right->scope) &&
         left->vertex_indices == right->vertex_indices);
}

bool finite_world_point(const ViewportWorldPoint& point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool selection_context_matches(
    const marrow::editor::SelectionSet& current,
    const marrow::editor::SelectionSet& captured) {
    if (current.items() != captured.items()) {
        return false;
    }
    const auto* current_active = current.active();
    const auto* captured_active = captured.active();
    return (current_active == nullptr && captured_active == nullptr) ||
        (current_active != nullptr && captured_active != nullptr &&
         *current_active == *captured_active);
}

bool offsets_equal(
    const std::vector<double>& left,
    const std::vector<double>& right,
    double epsilon = kOffsetEqualityEpsilon) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (!std::isfinite(left[index]) || !std::isfinite(right[index]) ||
            std::abs(left[index] - right[index]) > epsilon) {
            return false;
        }
    }
    return true;
}

const marrow::runtime::AttachmentData* linked_parent_attachment(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index,
    const marrow::runtime::AttachmentData& attachment) {
    if (!attachment.linked_mesh.has_value()) {
        return nullptr;
    }
    const auto& linked = *attachment.linked_mesh;
    if (linked.parent_skin_index.has_value()) {
        return skeleton.find_attachment(
            *linked.parent_skin_index,
            slot_index,
            linked.parent_attachment);
    }
    return skeleton.find_attachment_source(slot_index, linked.parent_attachment);
}

std::optional<ResolvedFfdTarget> resolve_target(const ShellState& state) {
    const auto* runtime_data = state.session.runtime_data();
    if (state.shell_mode != ShellMode::Animation || state.weight_paint.enabled ||
        state.selected_animation_name.empty() || !std::isfinite(state.timeline_time_seconds) ||
        state.preview_skeleton == nullptr || runtime_data == nullptr ||
        runtime_data->find_animation(state.selected_animation_name) == nullptr) {
        return std::nullopt;
    }

    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (!resolved.active_attachment.has_value() ||
        !resolved.active_attachment->skin_index.has_value()) {
        return std::nullopt;
    }
    const PreviewAttachmentSelection& selected = *resolved.active_attachment;
    if (selected.slot_index >= runtime_data->slots().size() ||
        selected.slot_index >= state.preview_skeleton->slot_states().size()) {
        return std::nullopt;
    }

    const auto* display_attachment = runtime_data->find_attachment(
        *selected.skin_index,
        selected.slot_index,
        selected.attachment_name);
    const auto* current_attachment =
        state.preview_skeleton->current_attachment(selected.slot_index);
    if (display_attachment == nullptr || current_attachment != display_attachment ||
        display_attachment->mesh_geometry == nullptr ||
        (display_attachment->kind != marrow::runtime::AttachmentKind::Mesh &&
         display_attachment->kind != marrow::runtime::AttachmentKind::LinkedMesh)) {
        return std::nullopt;
    }

    const marrow::runtime::AttachmentData* deform_attachment = display_attachment;
    std::string deform_attachment_name = display_attachment->name;
    if (display_attachment->linked_mesh.has_value() &&
        display_attachment->linked_mesh->deform) {
        deform_attachment_name =
            display_attachment->linked_mesh->parent_attachment;
        deform_attachment = linked_parent_attachment(
            *runtime_data, selected.slot_index, *display_attachment);
    }
    if (deform_attachment == nullptr || deform_attachment_name.empty() ||
        deform_attachment->name != deform_attachment_name ||
        deform_attachment->mesh_geometry == nullptr ||
        deform_attachment->mesh_geometry != display_attachment->mesh_geometry) {
        return std::nullopt;
    }

    const auto pose = state.preview_skeleton->evaluate_current_mesh_attachment(
        selected.slot_index);
    const std::size_t component_count =
        display_attachment->mesh_geometry->vertices.size();
    const std::size_t vertex_count = component_count / 2U;
    if (!pose.has_value() || pose->attachment_name != display_attachment->name ||
        component_count == 0U || (component_count % 2U) != 0U ||
        display_attachment->mesh_geometry->weights.size() != vertex_count ||
        pose->vertices.size() != vertex_count) {
        return std::nullopt;
    }

    ResolvedFfdTarget target;
    target.slot_index = selected.slot_index;
    target.display_skin_index = selected.skin_index;
    target.display_attachment_name = display_attachment->name;
    target.deform_attachment_name = std::move(deform_attachment_name);
    target.display_attachment = display_attachment;
    target.deform_attachment = deform_attachment;
    target.vertex_world_positions.reserve(vertex_count);
    for (const auto& vertex : pose->vertices) {
        const ViewportWorldPoint point{vertex.x, vertex.y};
        if (!finite_world_point(point)) {
            return std::nullopt;
        }
        target.vertex_world_positions.push_back(point);
    }
    return target;
}

std::optional<kernel::Matrix2> inverse_for_vertex(
    const ShellState& state,
    const ResolvedFfdTarget& target,
    std::size_t vertex_index) {
    if (target.display_attachment == nullptr ||
        target.display_attachment->mesh_geometry == nullptr ||
        vertex_index >= target.display_attachment->mesh_geometry->weights.size() ||
        state.preview_skeleton == nullptr) {
        return std::nullopt;
    }
    const auto& influences =
        target.display_attachment->mesh_geometry->weights[vertex_index].influences;
    const auto world = state.preview_skeleton->bone_world_transforms();
    std::vector<kernel::FfdInfluence> kernel_influences;
    kernel_influences.reserve(influences.size());
    for (const auto& influence : influences) {
        if (influence.bone_index >= world.size()) {
            return std::nullopt;
        }
        const auto transform = world[influence.bone_index];
        kernel_influences.push_back({
            {transform.a, transform.b, transform.c, transform.d},
            influence.weight});
    }
    return kernel::make_ffd_inverse(kernel_influences);
}

std::optional<std::vector<double>> sample_animation_offsets(
    const ShellState& state,
    const ResolvedFfdTarget& target) {
    const auto* runtime_data = state.session.runtime_data();
    const auto* animation = runtime_data != nullptr
        ? runtime_data->find_animation(state.selected_animation_name)
        : nullptr;
    if (animation == nullptr || target.display_attachment == nullptr ||
        target.display_attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }
    const std::size_t component_count =
        target.display_attachment->mesh_geometry->vertices.size();
    std::vector<double> offsets(component_count, 0.0);
    if (const auto sampled = animation->sample_slot_deform(
            target.slot_index,
            target.deform_attachment_name,
            state.timeline_time_seconds)) {
        offsets = *sampled;
    }
    if (offsets.size() != component_count ||
        !std::all_of(offsets.begin(), offsets.end(), [](double value) {
            return std::isfinite(value);
        })) {
        return std::nullopt;
    }
    return offsets;
}

bool materialized_edit_valid(
    const marrow::editor::MeshDeformTimelineEdit& edit,
    std::size_t component_count) {
    return std::all_of(
        edit.keyframes.begin(),
        edit.keyframes.end(),
        [&](const marrow::editor::DeformKeyframeEdit& keyframe) {
            return std::isfinite(keyframe.time) &&
                keyframe.vertex_offsets.size() == component_count &&
                std::all_of(
                    keyframe.vertex_offsets.begin(),
                    keyframe.vertex_offsets.end(),
                    [](double value) { return std::isfinite(value); });
        });
}

bool upsert_deform_keyframe(
    marrow::editor::MeshDeformTimelineEdit* edit,
    double time_seconds,
    const std::vector<double>& offsets) {
    if (edit == nullptr || !std::isfinite(time_seconds) || offsets.empty() ||
        !std::all_of(offsets.begin(), offsets.end(), [](double value) {
            return std::isfinite(value);
        })) {
        return false;
    }
    auto existing = marrow::editor::find_keyframe_near_time(
        edit->keyframes, time_seconds, 1e-6);
    if (existing != edit->keyframes.end()) {
        existing->vertex_offsets = offsets;
        return true;
    }
    marrow::editor::DeformKeyframeEdit keyframe;
    keyframe.time = time_seconds;
    keyframe.vertex_offsets = offsets;
    keyframe.interpolation = marrow::runtime::Interpolation::linear();
    const auto position = std::lower_bound(
        edit->keyframes.begin(),
        edit->keyframes.end(),
        time_seconds,
        [](const marrow::editor::DeformKeyframeEdit& candidate, double time) {
            return candidate.time < time;
        });
    edit->keyframes.insert(position, std::move(keyframe));
    return true;
}

bool context_valid(
    const ShellState& state,
    const ViewportFfdGesture& gesture,
    std::optional<ResolvedFfdTarget>* target_out = nullptr) {
    if (state.selected_animation_name != gesture.animation_name ||
        !std::isfinite(state.timeline_time_seconds) ||
        std::abs(state.timeline_time_seconds - gesture.time_seconds) > 1e-9 ||
        state.hierarchy_selection_anchor != gesture.hierarchy_anchor_before ||
        state.selected_timeline_track_id != gesture.timeline_focus_before ||
        !selection_context_matches(state.selection, gesture.selection_before) ||
        !ffd_selection_equal(
            state.viewport_ffd_selection,
            gesture.vertex_selection_before)) {
        return false;
    }
    auto target = resolve_target(state);
    if (!target.has_value() ||
        !scopes_equal(selection_scope(*target), gesture.scope) ||
        !valid_vertex_indices(gesture.vertex_indices, gesture.scope.vertex_count) ||
        !std::binary_search(
            gesture.vertex_indices.begin(),
            gesture.vertex_indices.end(),
            gesture.pressed_vertex_index) ||
        state.session.runtime_data()->find_animation(gesture.animation_name) == nullptr) {
        return false;
    }
    if (gesture.drag_started &&
        (gesture.start_vertex_offsets.size() != gesture.scope.vertex_count * 2U ||
         gesture.vertex_mappings.size() != gesture.vertex_indices.size() ||
         !gesture.transaction)) {
        return false;
    }
    if (target_out != nullptr) {
        *target_out = std::move(target);
    }
    return true;
}

void restore_context(ShellState* state, const ViewportFfdGesture& gesture) {
    state->selection = gesture.selection_before;
    state->viewport_ffd_selection = gesture.vertex_selection_before;
    state->hierarchy_selection_anchor = gesture.hierarchy_anchor_before;
    state->selected_timeline_track_id = gesture.timeline_focus_before;
}

bool world_position_matches(
    const ViewportWorldPoint& actual,
    const ViewportWorldPoint& expected) {
    if (!finite_world_point(actual) || !finite_world_point(expected)) {
        return false;
    }
    const double scale = std::max(
        {1.0, std::abs(expected.x), std::abs(expected.y)});
    return std::hypot(actual.x - expected.x, actual.y - expected.y) <=
        kWorldVerificationEpsilon * scale;
}

bool activate_group_drag(
    ShellState* state,
    const ResolvedFfdTarget& target,
    ViewportFfdGesture* gesture) {
    if (state == nullptr || gesture == nullptr || gesture->drag_started ||
        !scopes_equal(selection_scope(target), gesture->scope)) {
        return false;
    }
    const auto offsets = sample_animation_offsets(*state, target);
    if (!offsets.has_value()) {
        state->error_message =
            "Cannot sample a complete animation-only FFD vector for this group.";
        return false;
    }

    std::vector<ViewportFfdVertexMapping> mappings;
    mappings.reserve(gesture->vertex_indices.size());
    for (const std::size_t vertex_index : gesture->vertex_indices) {
        const auto inverse = inverse_for_vertex(*state, target, vertex_index);
        if (!inverse.has_value() ||
            vertex_index >= target.vertex_world_positions.size() ||
            !finite_world_point(target.vertex_world_positions[vertex_index])) {
            state->error_message =
                "Cannot move this FFD group because one selected vertex has an invalid weighted transform.";
            return false;
        }
        mappings.push_back({
            vertex_index,
            target.vertex_world_positions[vertex_index],
            inverse->a,
            inverse->b,
            inverse->c,
            inverse->d});
    }

    const std::size_t count = gesture->vertex_indices.size();
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::MoveBone,
        count == 1U
            ? "Move FFD vertex " + std::to_string(gesture->pressed_vertex_index)
            : "Move " + std::to_string(count) + " FFD vertices",
        "viewport-ffd:" + gesture->animation_name + ":" +
            std::to_string(gesture->scope.slot_index) + ":" +
            gesture->scope.deform_attachment_name,
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    auto* edit = marrow::editor::ensure_mesh_deform_timeline_edit(
        *transaction.project(),
        *state->session.runtime_data(),
        gesture->animation_name,
        state->session.runtime_data()->slots()[gesture->scope.slot_index].name,
        gesture->scope.deform_attachment_name);
    if (edit == nullptr ||
        !materialized_edit_valid(*edit, offsets->size())) {
        transaction.cancel();
        state->error_message =
            "Cannot materialize a complete FFD timeline for this attachment.";
        return false;
    }

    gesture->start_vertex_offsets = *offsets;
    gesture->vertex_mappings = std::move(mappings);
    gesture->transaction = std::move(transaction);
    gesture->drag_started = true;
    return true;
}

} // namespace

std::optional<ViewportFfdOverlay> build_overlay(const ShellState& state) {
    const auto target = resolve_target(state);
    if (!target.has_value()) {
        return std::nullopt;
    }
    ViewportFfdOverlay overlay;
    overlay.slot_index = target->slot_index;
    overlay.display_skin_index = target->display_skin_index;
    overlay.display_attachment_name = target->display_attachment_name;
    overlay.deform_attachment_name = target->deform_attachment_name;
    overlay.vertex_world_positions = target->vertex_world_positions;
    return overlay;
}

bool selection_matches_overlay(
    const ViewportFfdSelection& selection,
    const ViewportFfdOverlay& overlay) {
    return scopes_equal(selection.scope, selection_scope(overlay)) &&
        valid_vertex_indices(
            selection.vertex_indices,
            overlay.vertex_world_positions.size());
}

void reconcile_selection(ShellState* state) {
    if (state == nullptr || !state->viewport_ffd_selection.has_value()) {
        return;
    }
    const auto overlay = build_overlay(*state);
    if (!overlay.has_value() ||
        !selection_matches_overlay(*state->viewport_ffd_selection, *overlay)) {
        state->viewport_ffd_selection.reset();
    }
}

void clear_selection(ShellState* state) {
    if (state != nullptr) {
        state->viewport_ffd_selection.reset();
    }
}

bool select_vertex(
    ShellState* state,
    std::size_t vertex_index,
    bool toggle) {
    if (state == nullptr || authoring_gesture_active(*state)) {
        return false;
    }
    const auto overlay = build_overlay(*state);
    if (!overlay.has_value() ||
        vertex_index >= overlay->vertex_world_positions.size()) {
        return false;
    }
    std::vector<std::size_t> current;
    if (state->viewport_ffd_selection.has_value() &&
        selection_matches_overlay(*state->viewport_ffd_selection, *overlay)) {
        current = state->viewport_ffd_selection->vertex_indices;
    }
    const auto candidate = kernel::update_ffd_point_selection(
        current,
        overlay->vertex_world_positions.size(),
        vertex_index,
        toggle ? kernel::FfdPointSelectionMode::Toggle
               : kernel::FfdPointSelectionMode::Replace);
    if (!candidate.has_value()) {
        return false;
    }
    if (candidate->empty()) {
        state->viewport_ffd_selection.reset();
    } else {
        state->viewport_ffd_selection = ViewportFfdSelection{
            selection_scope(*overlay), *candidate};
    }
    return true;
}

std::optional<std::size_t> hit_test_vertex(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position) {
    const auto overlay = build_overlay(state);
    if (!overlay.has_value() || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
        return std::nullopt;
    }
    std::vector<kernel::Point> positions;
    positions.reserve(overlay->vertex_world_positions.size());
    for (const ViewportWorldPoint& vertex : overlay->vertex_world_positions) {
        const ImVec2 screen = screen_from_world(layout, vertex.x, vertex.y);
        positions.push_back({screen.x, screen.y});
    }
    return kernel::nearest_ffd_vertex(
        positions,
        {position.x, position.y},
        kVertexHandleHitRadius);
}

bool begin_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    std::size_t vertex_index,
    const ImVec2& pointer) {
    if (state == nullptr || authoring_gesture_active(*state) ||
        !state->load_result || state->load_result.project == nullptr ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        return false;
    }
    const auto target = resolve_target(*state);
    if (!target.has_value() || vertex_index >= target->vertex_world_positions.size()) {
        return false;
    }
    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    if (!finite_world_point(pointer_world)) {
        state->error_message = "Cannot begin an FFD gesture with an invalid pointer.";
        return false;
    }

    const ViewportFfdSelectionScope scope = selection_scope(*target);
    std::vector<std::size_t> selected_indices;
    if (state->viewport_ffd_selection.has_value() &&
        scopes_equal(state->viewport_ffd_selection->scope, scope) &&
        valid_vertex_indices(
            state->viewport_ffd_selection->vertex_indices,
            scope.vertex_count)) {
        selected_indices = state->viewport_ffd_selection->vertex_indices;
    }
    const bool pressed_was_selected = std::binary_search(
        selected_indices.begin(), selected_indices.end(), vertex_index);
    if (!pressed_was_selected) {
        const auto replaced = kernel::update_ffd_point_selection(
            selected_indices,
            scope.vertex_count,
            vertex_index,
            kernel::FfdPointSelectionMode::Replace);
        if (!replaced.has_value()) {
            return false;
        }
        selected_indices = *replaced;
        state->viewport_ffd_selection = ViewportFfdSelection{
            scope, selected_indices};
    }

    state->timeline_playing = false;
    state->session.set_playing(false);

    ViewportFfdGesture gesture;
    gesture.scope = scope;
    gesture.pressed_vertex_index = vertex_index;
    gesture.vertex_indices = selected_indices;
    gesture.animation_name = state->selected_animation_name;
    gesture.time_seconds = state->timeline_time_seconds;
    gesture.pointer_world_start = pointer_world;
    gesture.pointer_screen_start = pointer;
    gesture.pointer_screen = pointer;
    gesture.selection_before = state->selection;
    gesture.vertex_selection_before = state->viewport_ffd_selection;
    gesture.hierarchy_anchor_before = state->hierarchy_selection_anchor;
    gesture.timeline_focus_before = state->selected_timeline_track_id;
    gesture.collapse_to_pressed_on_click = pressed_was_selected &&
        selected_indices.size() > 1U;
    state->viewport_ffd_gesture.emplace(std::move(gesture));
    state->error_message.clear();
    return true;
}

bool update_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    if (state == nullptr || !state->viewport_ffd_gesture.has_value()) {
        return false;
    }
    auto& gesture = *state->viewport_ffd_gesture;
    std::optional<ResolvedFfdTarget> target;
    if (!context_valid(*state, gesture, &target) || !target.has_value() ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        finish_gesture(state, false);
        state->error_message =
            "FFD edit was cancelled after viewport context loss.";
        return false;
    }
    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    if (!finite_world_point(pointer_world)) {
        finish_gesture(state, false);
        state->error_message = "FFD edit was cancelled after invalid pointer math.";
        return false;
    }
    const kernel::Point world_delta{
        pointer_world.x - gesture.pointer_world_start.x,
        pointer_world.y - gesture.pointer_world_start.y};
    if (!gesture.drag_started) {
        const double screen_dx =
            static_cast<double>(pointer.x - gesture.pointer_screen_start.x);
        const double screen_dy =
            static_cast<double>(pointer.y - gesture.pointer_screen_start.y);
        const double distance_squared =
            (screen_dx * screen_dx) + (screen_dy * screen_dy);
        if (!std::isfinite(distance_squared) ||
            distance_squared < kernel::kFfdDragThreshold * kernel::kFfdDragThreshold) {
            gesture.pointer_screen = pointer;
            return std::isfinite(distance_squared);
        }
        if (!activate_group_drag(state, *target, &gesture)) {
            const std::string error = state->error_message;
            finish_gesture(state, false);
            state->error_message = error;
            return false;
        }
    }

    std::vector<kernel::FfdVertexDelta> local_deltas;
    local_deltas.reserve(gesture.vertex_mappings.size());
    for (const ViewportFfdVertexMapping& mapping : gesture.vertex_mappings) {
        const auto local_delta = kernel::map_ffd_delta(
            {mapping.inverse_a,
             mapping.inverse_b,
             mapping.inverse_c,
             mapping.inverse_d},
            world_delta);
        if (!local_delta.has_value()) {
            finish_gesture(state, false);
            state->error_message =
                "FFD edit was cancelled after invalid attachment-local math.";
            return false;
        }
        local_deltas.push_back({mapping.vertex_index, *local_delta});
    }
    const auto offsets = kernel::update_ffd_vertex_offsets(
        gesture.start_vertex_offsets, local_deltas);
    if (!offsets.has_value()) {
        finish_gesture(state, false);
        state->error_message =
            "FFD edit was cancelled because its group update was not atomic.";
        return false;
    }
    if (!gesture.changed && offsets_equal(*offsets, gesture.start_vertex_offsets)) {
        gesture.pointer_screen = pointer;
        return true;
    }

    auto* project = gesture.transaction.project();
    auto* edit = project != nullptr
        ? project->find_mesh_deform_timeline_edit(
              gesture.animation_name,
              state->session.runtime_data()->slots()[gesture.scope.slot_index].name,
              gesture.scope.deform_attachment_name)
        : nullptr;
    if (edit == nullptr ||
        !materialized_edit_valid(*edit, gesture.start_vertex_offsets.size()) ||
        !upsert_deform_keyframe(edit, gesture.time_seconds, *offsets)) {
        finish_gesture(state, false);
        state->error_message =
            "FFD edit was cancelled because its full-vector key is unavailable.";
        return false;
    }
    const marrow::editor::SessionResult refresh =
        gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_gesture(state, false);
        state->error_message = error;
        return false;
    }
    sync_shell_from_editor_session(state);

    std::optional<ResolvedFfdTarget> refreshed_target;
    if (!state->viewport_ffd_gesture.has_value() ||
        !context_valid(*state, *state->viewport_ffd_gesture, &refreshed_target) ||
        !refreshed_target.has_value()) {
        finish_gesture(state, false);
        state->error_message =
            "FFD edit was cancelled after runtime context changed during refresh.";
        return false;
    }
    for (const ViewportFfdVertexMapping& mapping : gesture.vertex_mappings) {
        const ViewportWorldPoint expected{
            mapping.vertex_world_start.x + world_delta.x,
            mapping.vertex_world_start.y + world_delta.y};
        const ViewportWorldPoint actual =
            refreshed_target->vertex_world_positions[mapping.vertex_index];
        if (!world_position_matches(actual, expected)) {
            const std::string error =
                "FFD edit was cancelled because downstream deformation overrode selected vertex " +
                std::to_string(mapping.vertex_index) + " in the atomic group.";
            finish_gesture(state, false);
            state->error_message = error;
            return false;
        }
    }
    gesture.pointer_screen = pointer;
    gesture.changed = !offsets_equal(*offsets, gesture.start_vertex_offsets);
    state->error_message.clear();
    return true;
}

void finish_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->viewport_ffd_gesture.has_value()) {
        return;
    }
    const bool valid = !commit || context_valid(
        *state, *state->viewport_ffd_gesture);
    ViewportFfdGesture gesture =
        std::move(*state->viewport_ffd_gesture);
    state->viewport_ffd_gesture.reset();
    if (!gesture.drag_started) {
        restore_context(state, gesture);
        if (commit && valid && gesture.collapse_to_pressed_on_click) {
            const auto collapsed = kernel::update_ffd_point_selection(
                gesture.vertex_indices,
                gesture.scope.vertex_count,
                gesture.pressed_vertex_index,
                kernel::FfdPointSelectionMode::Replace);
            if (collapsed.has_value()) {
                state->viewport_ffd_selection = ViewportFfdSelection{
                    gesture.scope, *collapsed};
            }
        }
        return;
    }
    const auto completion = kernel::completion_decision(
        commit, gesture.changed, valid);
    if (completion.action == kernel::CompletionAction::Cancel) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        restore_context(state, gesture);
        if (completion.report_cancelled) {
            state->status_message = "Cancelled FFD vertex edit";
        }
        return;
    }

    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    restore_context(state, gesture);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message = "FFD vertex edit failed";
        return;
    }
    state->error_message.clear();
    state->status_message = "Keyed " + gesture.scope.deform_attachment_name + " " +
        std::to_string(gesture.vertex_indices.size()) +
        (gesture.vertex_indices.size() == 1U ? " vertex at " : " vertices at ") +
        format_time_seconds(gesture.time_seconds);
}

bool begin_box_selection(
    ShellState* state,
    const ImVec2& pointer,
    bool additive) {
    if (state == nullptr || authoring_gesture_active(*state) ||
        state->viewport_box_selection.has_value() ||
        state->viewport_ffd_box_selection.has_value() ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        return false;
    }
    const auto overlay = build_overlay(*state);
    if (!overlay.has_value()) {
        return false;
    }
    reconcile_selection(state);
    state->viewport_ffd_box_selection = ViewportFfdBoxSelectionGesture{
        selection_scope(*overlay), pointer, pointer, additive, false};
    return true;
}

bool update_box_selection(
    ShellState* state,
    const ImVec2& pointer) {
    if (state == nullptr || !state->viewport_ffd_box_selection.has_value() ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        return false;
    }
    auto& box = *state->viewport_ffd_box_selection;
    box.current = pointer;
    const double dx = static_cast<double>(pointer.x - box.start.x);
    const double dy = static_cast<double>(pointer.y - box.start.y);
    box.dragged = box.dragged ||
        (dx * dx) + (dy * dy) >=
            kernel::kFfdDragThreshold * kernel::kFfdDragThreshold;
    return true;
}

bool finish_box_selection(
    ShellState* state,
    const ViewportLayout& layout,
    bool commit) {
    if (state == nullptr || !state->viewport_ffd_box_selection.has_value()) {
        return false;
    }
    const ViewportFfdBoxSelectionGesture box =
        *state->viewport_ffd_box_selection;
    state->viewport_ffd_box_selection.reset();
    if (!commit) {
        return false;
    }
    const auto overlay = build_overlay(*state);
    if (!overlay.has_value() ||
        !scopes_equal(selection_scope(*overlay), box.scope)) {
        reconcile_selection(state);
        return false;
    }

    const auto before = state->viewport_ffd_selection;
    if (!box.dragged) {
        if (!box.additive) {
            state->viewport_ffd_selection.reset();
        }
        return !ffd_selection_equal(before, state->viewport_ffd_selection);
    }

    std::vector<kernel::Point> positions;
    positions.reserve(overlay->vertex_world_positions.size());
    for (const ViewportWorldPoint& vertex : overlay->vertex_world_positions) {
        const ImVec2 screen = screen_from_world(layout, vertex.x, vertex.y);
        positions.push_back({screen.x, screen.y});
    }
    std::vector<std::size_t> current;
    if (state->viewport_ffd_selection.has_value() &&
        selection_matches_overlay(*state->viewport_ffd_selection, *overlay)) {
        current = state->viewport_ffd_selection->vertex_indices;
    }
    const auto candidate = kernel::update_ffd_box_selection(
        current,
        positions,
        {box.start.x, box.start.y},
        {box.current.x, box.current.y},
        box.additive);
    if (!candidate.has_value()) {
        return false;
    }
    if (candidate->empty()) {
        state->viewport_ffd_selection.reset();
    } else {
        state->viewport_ffd_selection = ViewportFfdSelection{
            box.scope, *candidate};
    }
    return !ffd_selection_equal(before, state->viewport_ffd_selection);
}

std::vector<std::size_t> box_preview_vertices(
    const ShellState& state,
    const ViewportLayout& layout) {
    if (!state.viewport_ffd_box_selection.has_value()) {
        return {};
    }
    const auto overlay = build_overlay(state);
    const auto& box = *state.viewport_ffd_box_selection;
    if (!overlay.has_value() ||
        !scopes_equal(selection_scope(*overlay), box.scope)) {
        return {};
    }
    std::vector<kernel::Point> positions;
    positions.reserve(overlay->vertex_world_positions.size());
    for (const ViewportWorldPoint& vertex : overlay->vertex_world_positions) {
        const ImVec2 screen = screen_from_world(layout, vertex.x, vertex.y);
        positions.push_back({screen.x, screen.y});
    }
    const auto collected = kernel::collect_ffd_vertices_in_box(
        positions,
        {box.start.x, box.start.y},
        {box.current.x, box.current.y});
    return collected.value_or(std::vector<std::size_t>{});
}

} // namespace marrow::editor::shell::viewport_ffd
