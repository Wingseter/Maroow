#include "shell_smoke_scenarios.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "shell_preview.hpp"
#include "shell_project_panels.hpp"
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "viewport_ffd_controller.hpp"
#include "viewport_interaction_kernel.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/runtime/module.hpp"

namespace marrow::editor::shell {
namespace {

constexpr double kFfdSmokeTime = 0.37;

bool selection_matches(
    const marrow::editor::SelectionSet& left,
    const marrow::editor::SelectionSet& right) {
    if (left.items() != right.items()) {
        return false;
    }
    const auto* left_active = left.active();
    const auto* right_active = right.active();
    return (left_active == nullptr && right_active == nullptr) ||
        (left_active != nullptr && right_active != nullptr &&
         *left_active == *right_active);
}

bool interpolation_matches(
    const marrow::runtime::Interpolation& left,
    const marrow::runtime::Interpolation& right) {
    if (left.kind() != right.kind()) {
        return false;
    }
    const auto& l = left.cubic_bezier();
    const auto& r = right.cubic_bezier();
    return l.cx1 == r.cx1 && l.cy1 == r.cy1 &&
        l.cx2 == r.cx2 && l.cy2 == r.cy2;
}

bool geometry_matches(
    const marrow::runtime::MeshGeometry& left,
    const marrow::runtime::MeshGeometry& right) {
    if (left.vertices != right.vertices || left.triangles != right.triangles ||
        left.uvs != right.uvs || left.weights.size() != right.weights.size()) {
        return false;
    }
    for (std::size_t vertex = 0U; vertex < left.weights.size(); ++vertex) {
        const auto& l = left.weights[vertex].influences;
        const auto& r = right.weights[vertex].influences;
        if (l.size() != r.size()) {
            return false;
        }
        for (std::size_t influence = 0U; influence < l.size(); ++influence) {
            if (l[influence].bone_index != r[influence].bone_index ||
                l[influence].x != r[influence].x ||
                l[influence].y != r[influence].y ||
                l[influence].weight != r[influence].weight) {
                return false;
            }
        }
    }
    return true;
}

std::string preview_signature(const marrow::runtime::Skeleton& skeleton) {
    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<double>::max_digits10);
    for (const auto& pose : skeleton.bone_poses()) {
        stream << pose.local_pose.x << ',' << pose.local_pose.y << ','
               << pose.local_pose.rotation << ',' << pose.local_pose.scale_x << ','
               << pose.local_pose.scale_y << ',' << pose.local_pose.shear_x << ','
               << pose.local_pose.shear_y << ',' << static_cast<int>(pose.inherit) << ';';
    }
    for (const auto& world : skeleton.bone_world_transforms()) {
        stream << world.a << ',' << world.b << ',' << world.c << ',' << world.d << ','
               << world.world_x << ',' << world.world_y << ';';
    }
    for (const auto& slot : skeleton.slot_states()) {
        stream << slot.attachment_name << ':';
        if (slot.attachment_skin_index.has_value()) {
            stream << *slot.attachment_skin_index;
        }
        stream << ';';
    }
    for (const auto& deform : skeleton.mesh_deform_states()) {
        stream << deform.attachment_name << ':';
        for (double value : deform.vertex_offsets) {
            stream << value << ',';
        }
        stream << ';';
    }
    return stream.str();
}

bool vector_matches(
    const std::vector<double>& left,
    const std::vector<double>& right,
    double epsilon = 1e-5) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (std::abs(left[index] - right[index]) > epsilon) {
            return false;
        }
    }
    return true;
}

bool ffd_selection_is(
    const ShellState& state,
    std::initializer_list<std::size_t> expected) {
    return state.viewport_ffd_selection.has_value() &&
        state.viewport_ffd_selection->vertex_indices ==
            std::vector<std::size_t>(expected);
}

bool ffd_selection_matches(
    const std::optional<ViewportFfdSelection>& left,
    const std::optional<ViewportFfdSelection>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
        (left->scope.slot_index == right->scope.slot_index &&
         left->scope.display_skin_index == right->scope.display_skin_index &&
         left->scope.display_attachment_name ==
             right->scope.display_attachment_name &&
         left->scope.deform_attachment_name ==
             right->scope.deform_attachment_name &&
         left->scope.vertex_count == right->scope.vertex_count &&
         left->vertex_indices == right->vertex_indices);
}

bool source_keys_preserved(
    const std::vector<marrow::runtime::DeformKeyframe>& source,
    const marrow::editor::MeshDeformTimelineEdit& materialized) {
    for (const auto& source_key : source) {
        const auto found = std::find_if(
            materialized.keyframes.begin(),
            materialized.keyframes.end(),
            [&](const marrow::editor::DeformKeyframeEdit& key) {
                return std::abs(
                    key.time - static_cast<double>(source_key.time)) <= 1e-6;
            });
        if (found == materialized.keyframes.end() ||
            found->vertex_offsets.size() != source_key.vertex_offsets.size() ||
            !interpolation_matches(found->interpolation, source_key.interpolation)) {
            return false;
        }
        for (std::size_t index = 0U;
             index < source_key.vertex_offsets.size();
             ++index) {
            if (std::abs(
                    found->vertex_offsets[index] -
                    static_cast<double>(source_key.vertex_offsets[index])) > 1e-6) {
                return false;
            }
        }
    }
    return true;
}

const marrow::editor::DeformKeyframeEdit* key_near(
    const marrow::editor::MeshDeformTimelineEdit* edit,
    double time) {
    if (edit == nullptr) {
        return nullptr;
    }
    const auto found = std::find_if(
        edit->keyframes.begin(),
        edit->keyframes.end(),
        [&](const marrow::editor::DeformKeyframeEdit& key) {
            return std::abs(key.time - time) <= 1e-6;
        });
    return found == edit->keyframes.end() ? nullptr : &*found;
}

struct TempDirectory {
    std::filesystem::path path;

    explicit TempDirectory(std::filesystem::path value)
        : path(std::move(value)) {}

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    TempDirectory(TempDirectory&& other) noexcept
        : path(std::move(other.path)) {
        other.path.clear();
    }

    TempDirectory& operator=(TempDirectory&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        std::error_code ignored;
        if (!path.empty()) {
            std::filesystem::remove_all(path, ignored);
        }
        path = std::move(other.path);
        other.path.clear();
        return *this;
    }

    ~TempDirectory() {
        std::error_code ignored;
        if (!path.empty()) {
            std::filesystem::remove_all(path, ignored);
        }
    }
};

std::optional<TempDirectory> make_temp_directory() {
    std::error_code error;
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    TempDirectory result(
        std::filesystem::temp_directory_path(error) /
        ("marrow-mar164-ffd-" + std::to_string(stamp)));
    if (error || !std::filesystem::create_directories(result.path, error) || error) {
        return std::nullopt;
    }
    return result;
}

bool prepare_attachment(
    ShellState* state,
    std::size_t slot_index,
    std::size_t skin_index,
    std::string attachment_name,
    double time) {
    if (!set_selected_animation(state, "idle", "FFD smoke", false, true) ||
        !scrub_timeline_time(state, time, "FFD smoke", false) ||
        !apply_attachment_selection_to_preview_slot(
            state,
            PreviewAttachmentSelection{slot_index, skin_index, attachment_name},
            "FFD smoke",
            false,
            false)) {
        return false;
    }
    const auto& data = *state->session.runtime_data();
    state->selection.replace(marrow::editor::BoneSelection{"root"});
    state->selection.toggle(marrow::editor::AttachmentSelection{
        data.slots()[slot_index].name,
        data.skins()[skin_index].name,
        std::move(attachment_name)});
    state->hierarchy_selection_anchor = marrow::editor::BoneSelection{"root"};
    state->selected_timeline_track_id = "viewport-ffd-focus";
    return true;
}

std::optional<ViewportLayout> smoke_layout(const ShellState& state) {
    return build_viewport_layout(
        state,
        ImVec2(17.0f, 29.0f),
        ImVec2(1200.0f, 760.0f));
}

std::optional<ImVec2> vertex_screen_position(
    const ShellState& state,
    const ViewportLayout& layout,
    std::size_t vertex_index) {
    const auto overlay = viewport_ffd::build_overlay(state);
    if (!overlay.has_value() || vertex_index >= overlay->vertex_world_positions.size()) {
        return std::nullopt;
    }
    const auto& vertex = overlay->vertex_world_positions[vertex_index];
    return screen_from_world(layout, vertex.x, vertex.y);
}

bool rollback_matches(
    const ShellState& state,
    const std::string& project,
    const marrow::runtime::SkeletonData* runtime,
    const std::string& preview,
    bool dirty,
    std::size_t undo_count,
    std::size_t redo_count,
    const marrow::editor::SelectionSet& selection,
    const std::optional<marrow::editor::SelectionItem>& anchor,
    const std::optional<std::string>& focus,
    const std::optional<ViewportFfdSelection>& ffd_selection) {
    return marrow::editor::serialize_project(*state.session.project()) == project &&
        state.session.runtime_data() == runtime &&
        preview_signature(*state.preview_skeleton) == preview &&
        state.session.dirty() == dirty &&
        state.session.undo_count() == undo_count &&
        state.session.redo_count() == redo_count &&
        !state.session.transaction_active() &&
        !state.viewport_ffd_gesture.has_value() &&
        selection_matches(state.selection, selection) &&
        ffd_selection_matches(state.viewport_ffd_selection, ffd_selection) &&
        state.hierarchy_selection_anchor == anchor &&
        state.selected_timeline_track_id == focus;
}

} // namespace

bool validate_viewport_ffd_smoke(const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state)) {
        std::cerr << state.error_message << '\n';
        return false;
    }
    const auto body_slot = state.session.runtime_data()->find_slot_index("body");
    const auto mesh_skin = state.session.runtime_data()->find_skin_index("mesh_base");
    const auto warrior_skin = state.session.runtime_data()->find_skin_index("warrior");
    if (!body_slot.has_value() || !mesh_skin.has_value() ||
        !warrior_skin.has_value() ||
        !prepare_attachment(
            &state, *body_slot, *mesh_skin, "body_mesh", kFfdSmokeTime)) {
        std::cerr << "FFD smoke could not stage body_mesh.\n";
        return false;
    }

    const auto* body_mesh = state.session.runtime_data()->find_attachment(
        *mesh_skin, *body_slot, "body_mesh");
    const auto* idle = state.session.runtime_data()->find_animation("idle");
    const auto* source_timeline = idle != nullptr
        ? idle->find_deform_timeline(*body_slot, "body_mesh")
        : nullptr;
    if (body_mesh == nullptr || body_mesh->mesh_geometry == nullptr ||
        source_timeline == nullptr || source_timeline->keyframes.size() != 3U) {
        std::cerr << "FFD smoke requires the body_mesh source timeline.\n";
        return false;
    }
    const marrow::runtime::MeshGeometry source_geometry = *body_mesh->mesh_geometry;
    const std::vector<marrow::runtime::DeformKeyframe> source_keys =
        source_timeline->keyframes;
    const auto initial_offsets = idle->sample_slot_deform(
        *body_slot, "body_mesh", kFfdSmokeTime);
    const auto layout = smoke_layout(state);
    const auto overlay = viewport_ffd::build_overlay(state);
    if (!layout.has_value() || !overlay.has_value() ||
        overlay->vertex_world_positions.size() != 4U ||
        overlay->display_attachment_name != "body_mesh" ||
        overlay->deform_attachment_name != "body_mesh" ||
        !initial_offsets.has_value() || initial_offsets->size() != 8U ||
        (*initial_offsets)[0] != 0.0 ||
        std::none_of(
            initial_offsets->begin() + 2,
            initial_offsets->end(),
            [](double value) { return value != 0.0; })) {
        std::cerr << "FFD smoke did not expose a full sparse/interpolated mesh pose.\n";
        return false;
    }
    const auto vertex_zero = vertex_screen_position(state, *layout, 0U);
    if (!vertex_zero.has_value() ||
        viewport_ffd::hit_test_vertex(state, *layout, *vertex_zero) !=
            std::optional<std::size_t>(0U)) {
        std::cerr << "FFD smoke could not hit the first 6px vertex handle.\n";
        return false;
    }
    const auto vertex_one = vertex_screen_position(state, *layout, 1U);
    if (!vertex_one.has_value() ||
        !viewport_ffd::select_vertex(&state, 0U, false) ||
        !ffd_selection_is(state, {0U}) ||
        !viewport_ffd::select_vertex(&state, 2U, true) ||
        !ffd_selection_is(state, {0U, 2U}) ||
        !viewport_ffd::select_vertex(&state, 0U, true) ||
        !ffd_selection_is(state, {2U}) ||
        state.viewport_ffd_gesture.has_value() ||
        state.session.transaction_active()) {
        std::cerr << "FFD point replace/toggle was not sorted, persistent, or toggle-only.\n";
        return false;
    }

    if (!viewport_ffd::select_vertex(&state, 0U, false) ||
        !viewport_ffd::select_vertex(&state, 1U, true) ||
        !viewport_ffd::begin_gesture(&state, *layout, 0U, *vertex_zero) ||
        !viewport_ffd::update_gesture(
            &state,
            *layout,
            ImVec2(vertex_zero->x + 3.0f, vertex_zero->y)) ||
        state.viewport_ffd_gesture->drag_started ||
        state.session.transaction_active()) {
        std::cerr << "FFD selected-vertex click crossed the 4px drag threshold early.\n";
        return false;
    }
    viewport_ffd::finish_gesture(&state, true);
    if (!ffd_selection_is(state, {0U}) || state.session.transaction_active()) {
        std::cerr << "FFD selected-click release did not collapse to its pressed vertex.\n";
        return false;
    }

    const ImVec2 one_box_start(vertex_one->x - 5.0f, vertex_one->y - 5.0f);
    const ImVec2 one_box_end(vertex_one->x, vertex_one->y);
    if (!viewport_ffd::begin_box_selection(&state, one_box_start, false) ||
        state.viewport_box_selection.has_value() ||
        !viewport_ffd::update_box_selection(&state, one_box_end) ||
        viewport_ffd::box_preview_vertices(state, *layout) !=
            std::vector<std::size_t>{1U} ||
        !viewport_ffd::finish_box_selection(&state, *layout, true) ||
        !ffd_selection_is(state, {1U})) {
        std::cerr << "FFD marquee did not take empty-space priority or replace inclusively.\n";
        return false;
    }
    if (!viewport_ffd::begin_box_selection(&state, one_box_end, false) ||
        !viewport_ffd::update_box_selection(&state, one_box_start) ||
        viewport_ffd::finish_box_selection(&state, *layout, true) ||
        !ffd_selection_is(state, {1U})) {
        std::cerr << "FFD reverse marquee did not preserve stable ordering.\n";
        return false;
    }
    const ImVec2 zero_box_start(vertex_zero->x - 5.0f, vertex_zero->y - 5.0f);
    const ImVec2 zero_box_end(vertex_zero->x, vertex_zero->y);
    if (!viewport_ffd::begin_box_selection(&state, zero_box_start, true) ||
        !viewport_ffd::update_box_selection(&state, zero_box_end) ||
        !viewport_ffd::finish_box_selection(&state, *layout, true) ||
        !ffd_selection_is(state, {0U, 1U})) {
        std::cerr << "FFD additive marquee did not merge stable vertex identities.\n";
        return false;
    }
    const ImVec2 empty_click(-1000.0f, -1000.0f);
    if (!viewport_ffd::begin_box_selection(&state, empty_click, true) ||
        viewport_ffd::finish_box_selection(&state, *layout, true) ||
        !ffd_selection_is(state, {0U, 1U}) ||
        !viewport_ffd::begin_box_selection(&state, empty_click, false) ||
        !viewport_ffd::finish_box_selection(&state, *layout, true) ||
        state.viewport_ffd_selection.has_value()) {
        std::cerr << "FFD empty click did not implement additive no-op/plain clear.\n";
        return false;
    }
    if (!viewport_ffd::select_vertex(&state, 0U, false)) {
        return false;
    }

    const std::string no_op_project =
        marrow::editor::serialize_project(*state.session.project());
    const std::size_t no_op_undo = state.session.undo_count();
    const bool no_op_dirty = state.session.dirty();
    const std::uint64_t no_op_project_revision = state.session.project_revision();
    const std::uint64_t no_op_runtime_revision = state.session.runtime_revision();
    const std::uint64_t no_op_preview_revision = state.session.preview_revision();
    const marrow::editor::SelectionSet no_op_selection = state.selection;
    const auto no_op_ffd_selection = state.viewport_ffd_selection;
    const auto no_op_anchor = state.hierarchy_selection_anchor;
    const auto no_op_focus = state.selected_timeline_track_id;
    if (!viewport_ffd::begin_gesture(&state, *layout, 0U, *vertex_zero) ||
        !state.viewport_ffd_gesture.has_value() ||
        state.viewport_ffd_gesture->drag_started ||
        !state.viewport_ffd_gesture->start_vertex_offsets.empty() ||
        state.viewport_ffd_gesture->vertex_indices !=
            std::vector<std::size_t>{0U} ||
        state.session.transaction_active()) {
        std::cerr << "FFD no-op press materialized before the 4px drag threshold.\n";
        return false;
    }
    viewport_ffd::finish_gesture(&state, true);
    if (marrow::editor::serialize_project(*state.session.project()) != no_op_project ||
        state.session.undo_count() != no_op_undo ||
        state.session.dirty() != no_op_dirty ||
        state.session.project_revision() != no_op_project_revision ||
        state.session.runtime_revision() != no_op_runtime_revision ||
        state.session.preview_revision() != no_op_preview_revision ||
        !selection_matches(state.selection, no_op_selection) ||
        !ffd_selection_matches(
            state.viewport_ffd_selection, no_op_ffd_selection) ||
        state.hierarchy_selection_anchor != no_op_anchor ||
        state.selected_timeline_track_id != no_op_focus) {
        std::cerr << "FFD click/no-movement changed project, revision, history, or selection.\n";
        return false;
    }

    const std::string single_before =
        marrow::editor::serialize_project(*state.session.project());
    if (!viewport_ffd::begin_gesture(&state, *layout, 0U, *vertex_zero) ||
        !viewport_ffd::update_gesture(
            &state,
            *layout,
            ImVec2(vertex_zero->x + 14.0f, vertex_zero->y - 9.0f))) {
        std::cerr << "Single-influence FFD update failed: " << state.error_message << '\n';
        return false;
    }
    viewport_ffd::finish_gesture(&state, true);
    const std::string single_after =
        marrow::editor::serialize_project(*state.session.project());
    const auto* single_edit = state.session.project()->find_mesh_deform_timeline_edit(
        "idle", "body", "body_mesh");
    const auto* single_key = key_near(single_edit, kFfdSmokeTime);
    if (single_after == single_before || single_edit == nullptr || single_key == nullptr ||
        single_key->vertex_offsets.size() != 8U ||
        single_key->interpolation.kind() !=
            marrow::runtime::InterpolationKind::Linear ||
        single_key->vertex_offsets[0] == (*initial_offsets)[0] ||
        single_key->vertex_offsets[1] == (*initial_offsets)[1] ||
        !std::equal(
            single_key->vertex_offsets.begin() + 2,
            single_key->vertex_offsets.end(),
            initial_offsets->begin() + 2) ||
        !source_keys_preserved(source_keys, *single_edit) ||
        state.session.undo_count() != no_op_undo + 1U ||
        !selection_matches(state.selection, no_op_selection) ||
        state.hierarchy_selection_anchor != no_op_anchor ||
        state.selected_timeline_track_id != no_op_focus) {
        std::cerr << "Single-influence FFD did not commit one full-vector key.\n";
        return false;
    }
    if (!undo_project_change(&state) ||
        marrow::editor::serialize_project(*state.session.project()) != single_before ||
        !redo_project_change(&state) ||
        marrow::editor::serialize_project(*state.session.project()) != single_after ||
        !undo_project_change(&state)) {
        std::cerr << "Single-influence FFD undo/redo was not atomic.\n";
        return false;
    }
    state.session.clear_history();

    if (!prepare_attachment(
            &state, *body_slot, *mesh_skin, "body_mesh", kFfdSmokeTime)) {
        return false;
    }
    const auto multi_layout = smoke_layout(state);
    const auto multi_vertex_one = multi_layout.has_value()
        ? vertex_screen_position(state, *multi_layout, 1U)
        : std::nullopt;
    const std::string multi_before =
        marrow::editor::serialize_project(*state.session.project());
    if (!multi_layout.has_value() || !multi_vertex_one.has_value() ||
        !viewport_ffd::begin_gesture(
            &state, *multi_layout, 1U, *multi_vertex_one) ||
        !viewport_ffd::update_gesture(
            &state,
            *multi_layout,
            ImVec2(
                multi_vertex_one->x - 11.0f,
                multi_vertex_one->y + 8.0f))) {
        std::cerr << "Multi-influence FFD update failed: " << state.error_message << '\n';
        return false;
    }
    viewport_ffd::finish_gesture(&state, true);
    const auto* multi_edit = state.session.project()->find_mesh_deform_timeline_edit(
        "idle", "body", "body_mesh");
    const auto* multi_key = key_near(multi_edit, kFfdSmokeTime);
    if (multi_key == nullptr || multi_key->vertex_offsets.size() != 8U ||
        multi_key->vertex_offsets[2] == (*initial_offsets)[2] ||
        multi_key->vertex_offsets[3] == (*initial_offsets)[3] ||
        multi_key->vertex_offsets[0] != (*initial_offsets)[0] ||
        multi_key->vertex_offsets[1] != (*initial_offsets)[1] ||
        !undo_project_change(&state) ||
        marrow::editor::serialize_project(*state.session.project()) != multi_before) {
        std::cerr << "Multi-influence FFD did not isolate one vertex pair.\n";
        return false;
    }
    state.session.clear_history();

    if (!prepare_attachment(
            &state, *body_slot, *mesh_skin, "body_mesh", kFfdSmokeTime) ||
        !viewport_ffd::select_vertex(&state, 0U, false) ||
        !viewport_ffd::select_vertex(&state, 1U, true)) {
        return false;
    }
    const auto group_layout = smoke_layout(state);
    const auto group_start = group_layout.has_value()
        ? vertex_screen_position(state, *group_layout, 0U)
        : std::nullopt;
    const auto group_overlay_before = viewport_ffd::build_overlay(state);
    const std::string group_before =
        marrow::editor::serialize_project(*state.session.project());
    const std::size_t group_undo_before = state.session.undo_count();
    const ImVec2 group_pointer = group_start.has_value()
        ? ImVec2(group_start->x + 13.0f, group_start->y - 7.0f)
        : ImVec2{};
    if (!group_layout.has_value() || !group_start.has_value() ||
        !group_overlay_before.has_value() ||
        !viewport_ffd::begin_gesture(
            &state, *group_layout, 0U, *group_start) ||
        !viewport_ffd::update_gesture(
            &state, *group_layout, group_pointer) ||
        !state.viewport_ffd_gesture.has_value() ||
        !state.viewport_ffd_gesture->drag_started ||
        state.viewport_ffd_gesture->vertex_mappings.size() != 2U ||
        !state.session.transaction_active()) {
        std::cerr << "FFD group drag did not validate and materialize after 4px.\n";
        return false;
    }
    viewport_ffd::finish_gesture(&state, true);
    const auto group_overlay_after = viewport_ffd::build_overlay(state);
    const auto* group_edit = state.session.project()->find_mesh_deform_timeline_edit(
        "idle", "body", "body_mesh");
    const auto* group_key = key_near(group_edit, kFfdSmokeTime);
    const ViewportWorldPoint group_world_start =
        world_from_screen(*group_layout, *group_start);
    const ViewportWorldPoint group_world_end =
        world_from_screen(*group_layout, group_pointer);
    const double group_dx = group_world_end.x - group_world_start.x;
    const double group_dy = group_world_end.y - group_world_start.y;
    const auto moved_by_common_delta = [&](std::size_t vertex_index) {
        const auto& before = group_overlay_before->vertex_world_positions[vertex_index];
        const auto& after = group_overlay_after->vertex_world_positions[vertex_index];
        return std::abs((after.x - before.x) - group_dx) <= 1e-4 &&
            std::abs((after.y - before.y) - group_dy) <= 1e-4;
    };
    if (!group_overlay_after.has_value() || group_key == nullptr ||
        group_key->vertex_offsets.size() != 8U ||
        group_key->vertex_offsets[0] == (*initial_offsets)[0] ||
        group_key->vertex_offsets[1] == (*initial_offsets)[1] ||
        group_key->vertex_offsets[2] == (*initial_offsets)[2] ||
        group_key->vertex_offsets[3] == (*initial_offsets)[3] ||
        !std::equal(
            group_key->vertex_offsets.begin() + 4,
            group_key->vertex_offsets.end(),
            initial_offsets->begin() + 4) ||
        !moved_by_common_delta(0U) || !moved_by_common_delta(1U) ||
        state.session.undo_count() != group_undo_before + 1U ||
        !ffd_selection_is(state, {0U, 1U})) {
        std::cerr << "FFD group did not move mixed influences by one world delta.\n";
        return false;
    }
    const std::string group_after =
        marrow::editor::serialize_project(*state.session.project());
    if (!undo_project_change(&state) ||
        marrow::editor::serialize_project(*state.session.project()) != group_before ||
        !ffd_selection_is(state, {0U, 1U}) ||
        !redo_project_change(&state) ||
        marrow::editor::serialize_project(*state.session.project()) != group_after ||
        !ffd_selection_is(state, {0U, 1U}) ||
        !undo_project_change(&state)) {
        std::cerr << "FFD group undo/redo was not one atomic history entry.\n";
        return false;
    }
    state.session.clear_history();

    const std::string return_project =
        marrow::editor::serialize_project(*state.session.project());
    const bool return_dirty = state.session.dirty();
    if (!viewport_ffd::begin_gesture(
            &state, *group_layout, 0U, *group_start) ||
        !viewport_ffd::update_gesture(
            &state,
            *group_layout,
            ImVec2(group_start->x + 9.0f, group_start->y + 5.0f)) ||
        !viewport_ffd::update_gesture(
            &state, *group_layout, *group_start)) {
        return false;
    }
    viewport_ffd::finish_gesture(&state, true);
    if (marrow::editor::serialize_project(*state.session.project()) != return_project ||
        state.session.dirty() != return_dirty ||
        state.session.undo_count() != 0U ||
        !ffd_selection_is(state, {0U, 1U})) {
        std::cerr << "FFD return-to-start did not cancel materialization and history.\n";
        return false;
    }

    if (!prepare_attachment(
            &state, *body_slot, *mesh_skin, "body_mesh", kFfdSmokeTime)) {
        return false;
    }
    const auto rollback_layout = smoke_layout(state);
    const auto rollback_vertex = rollback_layout.has_value()
        ? vertex_screen_position(state, *rollback_layout, 0U)
        : std::nullopt;
    const std::string rollback_project =
        marrow::editor::serialize_project(*state.session.project());
    const auto* rollback_runtime = state.session.runtime_data();
    const std::string rollback_preview = preview_signature(*state.preview_skeleton);
    const bool rollback_dirty = state.session.dirty();
    const std::size_t rollback_undo = state.session.undo_count();
    const std::size_t rollback_redo = state.session.redo_count();
    const marrow::editor::SelectionSet rollback_selection = state.selection;
    const auto rollback_ffd_selection = state.viewport_ffd_selection;
    const auto rollback_anchor = state.hierarchy_selection_anchor;
    const auto rollback_focus = state.selected_timeline_track_id;
    if (!rollback_layout.has_value() || !rollback_vertex.has_value() ||
        !viewport_ffd::begin_gesture(
            &state, *rollback_layout, 0U, *rollback_vertex) ||
        !viewport_ffd::update_gesture(
            &state,
            *rollback_layout,
            ImVec2(rollback_vertex->x + 9.0f, rollback_vertex->y + 5.0f))) {
        return false;
    }
    state.selected_timeline_track_id = "context-loss";
    if (viewport_ffd::update_gesture(
            &state,
            *rollback_layout,
            ImVec2(rollback_vertex->x + 12.0f, rollback_vertex->y + 7.0f)) ||
        !rollback_matches(
            state,
            rollback_project,
            rollback_runtime,
            rollback_preview,
            rollback_dirty,
            rollback_undo,
            rollback_redo,
            rollback_selection,
            rollback_anchor,
            rollback_focus,
            rollback_ffd_selection)) {
        std::cerr << "FFD context-loss rollback was not exact.\n";
        return false;
    }

    if (!viewport_ffd::begin_gesture(
            &state, *rollback_layout, 0U, *rollback_vertex) ||
        !viewport_ffd::update_gesture(
            &state,
            *rollback_layout,
            ImVec2(rollback_vertex->x + 10.0f, rollback_vertex->y - 4.0f))) {
        return false;
    }
    viewport_ffd::finish_gesture(&state, false);
    if (!rollback_matches(
            state,
            rollback_project,
            rollback_runtime,
            rollback_preview,
            rollback_dirty,
            rollback_undo,
            rollback_redo,
            rollback_selection,
            rollback_anchor,
            rollback_focus,
            rollback_ffd_selection)) {
        std::cerr << "FFD explicit Escape/focus cancellation was not exact.\n";
        return false;
    }

    if (!viewport_ffd::begin_gesture(
            &state, *rollback_layout, 0U, *rollback_vertex) ||
        !viewport_ffd::update_gesture(
            &state,
            *rollback_layout,
            ImVec2(rollback_vertex->x + 8.0f, rollback_vertex->y - 6.0f))) {
        return false;
    }
    auto* invalid_edit = state.viewport_ffd_gesture->transaction.project()
        ->find_mesh_deform_timeline_edit("idle", "body", "body_mesh");
    if (invalid_edit == nullptr || invalid_edit->keyframes.empty()) {
        return false;
    }
    invalid_edit->keyframes.front().vertex_offsets.pop_back();
    if (viewport_ffd::update_gesture(
            &state,
            *rollback_layout,
            ImVec2(rollback_vertex->x + 13.0f, rollback_vertex->y - 7.0f)) ||
        !rollback_matches(
            state,
            rollback_project,
            rollback_runtime,
            rollback_preview,
            rollback_dirty,
            rollback_undo,
            rollback_redo,
            rollback_selection,
            rollback_anchor,
            rollback_focus,
            rollback_ffd_selection)) {
        std::cerr << "FFD invalid-payload rollback was not exact.\n";
        return false;
    }

    if (!viewport_ffd::begin_gesture(
            &state, *rollback_layout, 0U, *rollback_vertex) ||
        !viewport_ffd::update_gesture(
            &state,
            *rollback_layout,
            ImVec2(rollback_vertex->x + 7.0f, rollback_vertex->y + 3.0f))) {
        return false;
    }
    state.viewport_ffd_gesture->transaction.project()
        ->mesh_deform_timeline_edits.push_back({});
    if (viewport_ffd::update_gesture(
            &state,
            *rollback_layout,
            ImVec2(rollback_vertex->x + 10.0f, rollback_vertex->y + 4.0f)) ||
        !rollback_matches(
            state,
            rollback_project,
            rollback_runtime,
            rollback_preview,
            rollback_dirty,
            rollback_undo,
            rollback_redo,
            rollback_selection,
            rollback_anchor,
            rollback_focus,
            rollback_ffd_selection)) {
        std::cerr << "FFD runtime-refresh failure did not restore the transaction.\n";
        return false;
    }

    const auto spine_index = state.session.runtime_data()->find_bone_index("spine");
    const auto arm_index = state.session.runtime_data()->find_bone_index("arm_l");
    const auto* current_body_mesh = state.session.runtime_data()->find_attachment(
        *mesh_skin, *body_slot, "body_mesh");
    if (!spine_index.has_value() || !arm_index.has_value() ||
        current_body_mesh == nullptr || current_body_mesh->mesh_geometry == nullptr ||
        current_body_mesh->mesh_geometry->weights.size() < 2U) {
        return false;
    }
    const auto pose_before_singular = state.preview_skeleton->bone_poses();
    state.preview_skeleton->bone_poses()[*spine_index].local_pose.scale_x = 0.0;
    state.preview_skeleton->bone_poses()[*arm_index].inherit =
        marrow::runtime::BoneInherit::NoScale;
    state.preview_skeleton->update_world_transforms();
    const auto singular_world = state.preview_skeleton->bone_world_transforms();
    const auto inverse_for_smoke_vertex = [&](std::size_t vertex_index) {
        std::vector<marrow::editor::viewport_interaction_kernel::FfdInfluence>
            influences;
        for (const auto& influence :
             current_body_mesh->mesh_geometry->weights[vertex_index].influences) {
            const auto world = singular_world[influence.bone_index];
            influences.push_back({
                {world.a, world.b, world.c, world.d}, influence.weight});
        }
        return marrow::editor::viewport_interaction_kernel::make_ffd_inverse(
            influences);
    };
    const auto singular_inverse = inverse_for_smoke_vertex(0U);
    const auto valid_inverse = inverse_for_smoke_vertex(1U);
    const auto singular_layout = smoke_layout(state);
    const auto singular_vertex = singular_layout.has_value()
        ? vertex_screen_position(state, *singular_layout, 0U)
        : std::nullopt;
    const std::string singular_project =
        marrow::editor::serialize_project(*state.session.project());
    const std::uint64_t singular_project_revision = state.session.project_revision();
    const std::uint64_t singular_runtime_revision = state.session.runtime_revision();
    const std::uint64_t singular_preview_revision = state.session.preview_revision();
    const std::size_t singular_undo = state.session.undo_count();
    if (singular_inverse.has_value() || !valid_inverse.has_value() ||
        !singular_layout.has_value() || !singular_vertex.has_value() ||
        !viewport_ffd::select_vertex(&state, 0U, false) ||
        !viewport_ffd::select_vertex(&state, 1U, true) ||
        !viewport_ffd::begin_gesture(
            &state, *singular_layout, 0U, *singular_vertex) ||
        viewport_ffd::update_gesture(
            &state,
            *singular_layout,
            ImVec2(singular_vertex->x + 8.0f, singular_vertex->y + 5.0f)) ||
        state.viewport_ffd_gesture.has_value() ||
        state.session.transaction_active() ||
        marrow::editor::serialize_project(*state.session.project()) !=
            singular_project ||
        state.session.project_revision() != singular_project_revision ||
        state.session.runtime_revision() != singular_runtime_revision ||
        state.session.preview_revision() != singular_preview_revision ||
        state.session.undo_count() != singular_undo ||
        !ffd_selection_is(state, {0U, 1U})) {
        std::cerr << "FFD mixed-solvability group mutated before atomic rejection.\n";
        return false;
    }
    state.preview_skeleton->bone_poses() = pose_before_singular;
    state.preview_skeleton->update_world_transforms();

    const auto persistent_selection = state.viewport_ffd_selection;
    if (!scrub_timeline_time(
            &state, 0.41, "FFD selection persistence", false) ||
        !ffd_selection_matches(
            state.viewport_ffd_selection, persistent_selection) ||
        !set_selected_animation(
            &state, "attack", "FFD selection persistence", false, false) ||
        !ffd_selection_matches(
            state.viewport_ffd_selection, persistent_selection) ||
        !set_selected_animation(
            &state, "idle", "FFD selection persistence", false, false) ||
        !scrub_timeline_time(
            &state, kFfdSmokeTime, "FFD selection persistence", false) ||
        !ffd_selection_matches(
            state.viewport_ffd_selection, persistent_selection)) {
        std::cerr << "FFD selection did not survive time/animation preview changes.\n";
        return false;
    }
    apply_shell_mode(&state, ShellMode::Parameter);
    if (state.viewport_ffd_selection.has_value()) {
        std::cerr << "FFD selection survived a mode change.\n";
        return false;
    }
    apply_shell_mode(&state, ShellMode::Animation);

    if (!prepare_attachment(
            &state, *body_slot, *warrior_skin, "warrior_body", kFfdSmokeTime)) {
        std::cerr << "FFD smoke could not stage warrior_body.\n";
        return false;
    }
    if (state.viewport_ffd_selection.has_value()) {
        std::cerr << "FFD selection survived an exact attachment/skin target change.\n";
        return false;
    }
    const marrow::editor::SelectionSet linked_selection = state.selection;
    const auto linked_anchor = state.hierarchy_selection_anchor;
    const auto linked_focus = state.selected_timeline_track_id;
    const auto linked_layout = smoke_layout(state);
    const auto linked_overlay = viewport_ffd::build_overlay(state);
    const auto linked_vertex = linked_layout.has_value()
        ? vertex_screen_position(state, *linked_layout, 0U)
        : std::nullopt;
    if (!linked_layout.has_value() || !linked_overlay.has_value() ||
        !linked_vertex.has_value() ||
        linked_overlay->display_attachment_name != "warrior_body" ||
        linked_overlay->deform_attachment_name != "body_mesh" ||
        state.preview_skeleton->current_mesh_vertex_offsets(*body_slot) == nullptr ||
        !viewport_ffd::select_vertex(&state, 0U, false) ||
        !viewport_ffd::select_vertex(&state, 1U, true) ||
        !viewport_ffd::begin_gesture(
            &state, *linked_layout, 0U, *linked_vertex) ||
        state.viewport_ffd_gesture->scope.deform_attachment_name != "body_mesh" ||
        state.viewport_ffd_gesture->vertex_indices !=
            std::vector<std::size_t>({0U, 1U}) ||
        !viewport_ffd::update_gesture(
            &state,
            *linked_layout,
            ImVec2(linked_vertex->x + 12.0f, linked_vertex->y + 4.0f))) {
        std::cerr << "Linked-mesh FFD target/update failed: " << state.error_message << '\n';
        return false;
    }
    viewport_ffd::finish_gesture(&state, true);
    const auto* linked_edit = state.session.project()->find_mesh_deform_timeline_edit(
        "idle", "body", "body_mesh");
    const auto* linked_key = key_near(linked_edit, kFfdSmokeTime);
    const auto refreshed_mesh_skin = state.session.runtime_data()->find_skin_index("mesh_base");
    const auto refreshed_warrior_skin = state.session.runtime_data()->find_skin_index("warrior");
    const auto* refreshed_body = refreshed_mesh_skin.has_value()
        ? state.session.runtime_data()->find_attachment(
              *refreshed_mesh_skin, *body_slot, "body_mesh")
        : nullptr;
    const auto* refreshed_warrior = refreshed_warrior_skin.has_value()
        ? state.session.runtime_data()->find_attachment(
              *refreshed_warrior_skin, *body_slot, "warrior_body")
        : nullptr;
    if (linked_key == nullptr || linked_key->vertex_offsets.size() != 8U ||
        linked_key->vertex_offsets[0] == (*initial_offsets)[0] ||
        linked_key->vertex_offsets[1] == (*initial_offsets)[1] ||
        linked_key->vertex_offsets[2] == (*initial_offsets)[2] ||
        linked_key->vertex_offsets[3] == (*initial_offsets)[3] ||
        state.session.project()->find_mesh_deform_timeline_edit(
            "idle", "body", "warrior_body") != nullptr ||
        refreshed_body == nullptr || refreshed_warrior == nullptr ||
        refreshed_body->mesh_geometry == nullptr ||
        refreshed_warrior->mesh_geometry != refreshed_body->mesh_geometry ||
        !geometry_matches(source_geometry, *refreshed_body->mesh_geometry) ||
        !refreshed_warrior->linked_mesh.has_value() ||
        refreshed_warrior->linked_mesh->parent_attachment != "body_mesh" ||
        !refreshed_warrior->linked_mesh->deform ||
        !ffd_selection_is(state, {0U, 1U}) ||
        !selection_matches(state.selection, linked_selection) ||
        state.hierarchy_selection_anchor != linked_anchor ||
        state.selected_timeline_track_id != linked_focus) {
        std::cerr << "Linked FFD changed target identity, topology, weights, or context.\n";
        return false;
    }

    const std::vector<double> committed_offsets = linked_key->vertex_offsets;
    auto temporary = make_temp_directory();
    if (!temporary.has_value()) {
        std::cerr << "FFD smoke could not create its temporary export directory.\n";
        return false;
    }
    marrow::editor::ProjectData save_copy = *state.session.project();
    save_copy.runtime_assets.skeleton_path =
        std::filesystem::absolute(
            state.session.project()->resolved_skeleton_path());
    save_copy.runtime_assets.atlas_paths =
        state.session.project()->resolved_atlas_paths();
    for (auto& atlas_path : save_copy.runtime_assets.atlas_paths) {
        atlas_path = std::filesystem::absolute(atlas_path);
    }
    const std::filesystem::path saved_project =
        temporary->path / "marrow_mar164.marrow";
    const auto saved = marrow::editor::save_project(save_copy, saved_project);
    const auto reloaded = saved
        ? marrow::editor::load_project(saved_project)
        : marrow::editor::ProjectLoadResult{};
    const auto* reloaded_edit = reloaded && reloaded.project != nullptr
        ? reloaded.project->find_mesh_deform_timeline_edit(
              "idle", "body", "body_mesh")
        : nullptr;
    const auto* reloaded_key = key_near(reloaded_edit, kFfdSmokeTime);
    if (!saved || !reloaded || reloaded_key == nullptr ||
        !vector_matches(reloaded_key->vertex_offsets, committed_offsets, 1e-9) ||
        !source_keys_preserved(source_keys, *reloaded_edit)) {
        std::cerr << "FFD full-vector project save/reload failed.\n";
        if (!saved && saved.error.has_value()) {
            std::cerr << saved.error->format() << '\n';
        }
        if (saved && !reloaded && reloaded.error.has_value()) {
            std::cerr << reloaded.error->format() << '\n';
        }
        if (reloaded_edit != nullptr) {
            std::cerr << "Reloaded deform keys=" << reloaded_edit->keyframes.size()
                      << " source_preserved="
                      << source_keys_preserved(source_keys, *reloaded_edit) << '\n';
        }
        return false;
    }

    const auto selection_before_failed_adoption = state.viewport_ffd_selection;
    state.project_path = temporary->path / "missing-project.marrow";
    if (reload_project(&state) ||
        !ffd_selection_matches(
            state.viewport_ffd_selection,
            selection_before_failed_adoption)) {
        std::cerr << "Failed project adoption did not preserve FFD selection.\n";
        return false;
    }
    state.project_path = saved_project;
    if (!reload_project(&state) || state.viewport_ffd_selection.has_value() ||
        state.viewport_ffd_box_selection.has_value()) {
        std::cerr << "Successful project adoption did not clear transient FFD selection.\n";
        return false;
    }

    marrow::editor::ProjectExportOptions export_options;
    export_options.skeleton_output_path = temporary->path / "marrow_mar164.mskl";
    export_options.binary_output_path = temporary->path / "marrow_mar164.mbin";
    const auto exported = marrow::editor::export_runtime_assets(
        *state.session.project(),
        *state.session.base_skeleton_document(),
        export_options);
    const auto json_runtime = exported
        ? marrow::runtime::load_skeleton_data(export_options.skeleton_output_path)
        : marrow::runtime::SkeletonDataResult{};
    const auto binary_runtime = exported && export_options.binary_output_path.has_value()
        ? marrow::runtime::load_skeleton_data(*export_options.binary_output_path)
        : marrow::runtime::SkeletonDataResult{};
    const auto json_body_slot = json_runtime
        ? json_runtime.skeleton_data->find_slot_index("body")
        : std::nullopt;
    const auto binary_body_slot = binary_runtime
        ? binary_runtime.skeleton_data->find_slot_index("body")
        : std::nullopt;
    const auto* json_idle = json_runtime
        ? json_runtime.skeleton_data->find_animation("idle")
        : nullptr;
    const auto* binary_idle = binary_runtime
        ? binary_runtime.skeleton_data->find_animation("idle")
        : nullptr;
    const auto json_offsets = json_idle != nullptr && json_body_slot.has_value()
        ? json_idle->sample_slot_deform(
              *json_body_slot, "body_mesh", kFfdSmokeTime)
        : std::nullopt;
    const auto binary_offsets = binary_idle != nullptr && binary_body_slot.has_value()
        ? binary_idle->sample_slot_deform(
              *binary_body_slot, "body_mesh", kFfdSmokeTime)
        : std::nullopt;
    if (!exported || !json_runtime || !binary_runtime ||
        !json_offsets.has_value() || !binary_offsets.has_value() ||
        json_idle->find_deform_timeline(*json_body_slot, "body_mesh") == nullptr ||
        binary_idle->find_deform_timeline(*binary_body_slot, "body_mesh") == nullptr ||
        json_idle->find_deform_timeline(*json_body_slot, "warrior_body") != nullptr ||
        binary_idle->find_deform_timeline(*binary_body_slot, "warrior_body") != nullptr ||
        !vector_matches(*json_offsets, committed_offsets) ||
        !vector_matches(*binary_offsets, committed_offsets)) {
        std::cerr << "FFD JSON/MBIN full-vector target round-trip failed.\n";
        return false;
    }

    ShellState override_state;
    override_state.project_path =
        project_path.parent_path() / "parameter_face_basic.marrow";
    if (!reload_project(&override_state)) {
        return false;
    }
    const auto override_face_slot =
        override_state.session.runtime_data()->find_slot_index("face");
    const auto override_default_skin =
        override_state.session.runtime_data()->find_skin_index("default");
    if (!override_face_slot.has_value() || !override_default_skin.has_value() ||
        !prepare_attachment(
            &override_state,
            *override_face_slot,
            *override_default_skin,
            "face_mesh",
            kFfdSmokeTime) ||
        !viewport_ffd::select_vertex(&override_state, 0U, false) ||
        !viewport_ffd::select_vertex(&override_state, 1U, true) ||
        !override_state.session.set_preview_parameter_value("mouth.open", 0.5)) {
        return false;
    }
    sync_shell_from_editor_session(&override_state);
    override_state.session.clear_history();
    const auto override_layout = smoke_layout(override_state);
    const auto override_vertex = override_layout.has_value()
        ? vertex_screen_position(override_state, *override_layout, 0U)
        : std::nullopt;
    const std::string override_project =
        marrow::editor::serialize_project(*override_state.session.project());
    const auto override_selection = override_state.viewport_ffd_selection;
    const auto* override_runtime = override_state.session.runtime_data();
    const std::string override_preview_signature =
        preview_signature(*override_state.preview_skeleton);
    const bool override_dirty = override_state.session.dirty();
    if (!override_layout.has_value() || !override_vertex.has_value() ||
        !viewport_ffd::begin_gesture(
            &override_state, *override_layout, 0U, *override_vertex) ||
        viewport_ffd::update_gesture(
            &override_state,
            *override_layout,
            ImVec2(override_vertex->x + 10.0f, override_vertex->y - 6.0f)) ||
        override_state.viewport_ffd_gesture.has_value() ||
        override_state.session.transaction_active() ||
        marrow::editor::serialize_project(*override_state.session.project()) !=
            override_project ||
        override_state.session.runtime_data() != override_runtime ||
        preview_signature(*override_state.preview_skeleton) !=
            override_preview_signature ||
        override_state.session.dirty() != override_dirty ||
        override_state.session.undo_count() != 0U ||
        !ffd_selection_matches(
            override_state.viewport_ffd_selection, override_selection)) {
        std::cerr << "FFD downstream parameter override did not roll back atomically.\n";
        return false;
    }

    ShellState parameter_state;
    parameter_state.project_path =
        project_path.parent_path() / "parameter_face_basic.marrow";
    if (!reload_project(&parameter_state)) {
        std::cerr << "FFD parameter-composition smoke could not load its fixture.\n";
        return false;
    }
    auto parameter_transaction = parameter_state.session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Stage additive FFD parameter smoke",
        "smoke:ffd-parameter",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    auto* parameter_shape = parameter_transaction && parameter_transaction.project() &&
            parameter_transaction.project()->parameter_model.has_value()
        ? parameter_transaction.project()->parameter_model->find_shape(
              "mouth.open.shape")
        : nullptr;
    if (parameter_shape == nullptr) {
        return false;
    }
    parameter_shape->blend_mode =
        marrow::runtime::ParameterShapeBlendMode::AdditiveClamped;
    const auto parameter_refresh = parameter_transaction.refresh_runtime();
    if (!parameter_refresh) {
        return false;
    }
    sync_shell_from_editor_session(&parameter_state);
    const auto parameter_commit = parameter_transaction.commit();
    if (!parameter_commit) {
        return false;
    }
    sync_shell_from_editor_session(&parameter_state);
    parameter_state.session.clear_history();

    const auto face_slot = parameter_state.session.runtime_data()->find_slot_index("face");
    const auto default_skin = parameter_state.session.runtime_data()->find_skin_index("default");
    if (!face_slot.has_value() || !default_skin.has_value() ||
        !prepare_attachment(
            &parameter_state,
            *face_slot,
            *default_skin,
            "face_mesh",
            kFfdSmokeTime) ||
        !viewport_ffd::select_vertex(&parameter_state, 0U, false) ||
        !viewport_ffd::select_vertex(&parameter_state, 1U, true)) {
        return false;
    }
    const auto parameter_selection_before =
        parameter_state.viewport_ffd_selection;
    const auto parameter_preview =
        parameter_state.session.set_preview_parameter_value("mouth.open", 0.5);
    if (!parameter_preview) {
        return false;
    }
    sync_shell_from_editor_session(&parameter_state);
    viewport_ffd::reconcile_selection(&parameter_state);
    parameter_state.session.clear_history();
    const auto* parameter_animation =
        parameter_state.session.runtime_data()->find_animation("idle");
    const auto animation_only_offsets = parameter_animation != nullptr
        ? parameter_animation->sample_slot_deform(
              *face_slot, "face_mesh", kFfdSmokeTime)
        : std::nullopt;
    const auto* final_offsets_pointer =
        parameter_state.preview_skeleton->current_final_mesh_vertex_offsets(
            *face_slot);
    const auto* parameter_attachment =
        parameter_state.session.runtime_data()->find_attachment(
            *default_skin, *face_slot, "face_mesh");
    const auto parameter_layout = smoke_layout(parameter_state);
    const auto parameter_vertex = parameter_layout.has_value()
        ? vertex_screen_position(parameter_state, *parameter_layout, 0U)
        : std::nullopt;
    if (!ffd_selection_matches(
            parameter_state.viewport_ffd_selection,
            parameter_selection_before) ||
        !animation_only_offsets.has_value() || final_offsets_pointer == nullptr ||
        vector_matches(*final_offsets_pointer, *animation_only_offsets, 1e-9) ||
        parameter_attachment == nullptr || parameter_attachment->mesh_geometry == nullptr ||
        !parameter_layout.has_value() || !parameter_vertex.has_value()) {
        std::cerr << "FFD parameter preview did not remain a separate composed layer.\n";
        return false;
    }
    const marrow::runtime::MeshGeometry parameter_geometry =
        *parameter_attachment->mesh_geometry;
    if (!viewport_ffd::begin_gesture(
            &parameter_state,
            *parameter_layout,
            0U,
            *parameter_vertex) ||
        !viewport_ffd::update_gesture(
            &parameter_state,
            *parameter_layout,
            ImVec2(parameter_vertex->x + 10.0f, parameter_vertex->y - 6.0f)) ||
        !parameter_state.viewport_ffd_gesture.has_value() ||
        parameter_state.viewport_ffd_gesture->start_vertex_offsets !=
            *animation_only_offsets) {
        std::cerr << "FFD group baked parameter output into its animation start vector.\n";
        return false;
    }
    viewport_ffd::finish_gesture(&parameter_state, true);
    const auto* parameter_edit =
        parameter_state.session.project()->find_mesh_deform_timeline_edit(
            "idle", "face", "face_mesh");
    const auto* parameter_key = key_near(parameter_edit, kFfdSmokeTime);
    const auto* parameter_attachment_after =
        parameter_state.session.runtime_data()->find_attachment(
            *default_skin, *face_slot, "face_mesh");
    if (parameter_key == nullptr || parameter_key->vertex_offsets.size() != 8U ||
        parameter_key->vertex_offsets[0] == (*animation_only_offsets)[0] ||
        parameter_key->vertex_offsets[1] == (*animation_only_offsets)[1] ||
        parameter_key->vertex_offsets[2] == (*animation_only_offsets)[2] ||
        parameter_key->vertex_offsets[3] == (*animation_only_offsets)[3] ||
        !std::equal(
            parameter_key->vertex_offsets.begin() + 4,
            parameter_key->vertex_offsets.end(),
            animation_only_offsets->begin() + 4) ||
        parameter_attachment_after == nullptr ||
        parameter_attachment_after->mesh_geometry == nullptr ||
        !geometry_matches(
            parameter_geometry, *parameter_attachment_after->mesh_geometry) ||
        parameter_state.session.undo_count() != 1U ||
        !ffd_selection_is(parameter_state, {0U, 1U})) {
        std::cerr << "FFD parameter-composed group did not stay animation-only.\n";
        return false;
    }

    std::cout << "MAR-164 attachment-local multi-vertex FFD smoke passed.\n";
    return true;
}

} // namespace marrow::editor::shell
