#include "shell_viewport_ui.hpp"

#include "shell_preview.hpp"
#include "shell_project_panels.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_selection.hpp"
#include "shell_theme.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "shell_widgets.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;
using marrow::editor::IconRegistry;

void auto_frame_skeleton(ShellState* state, ImVec2 canvas_size) {
    (void)canvas_size;
    if (frame_viewport_camera_to_preview_pose(state)) {
        state->status_message = "Framed skeleton to viewport";
    }
}

namespace {

constexpr float kTranslateGizmoLength = 42.0f;
constexpr float kTranslateGizmoHitRadius = 7.0f;

std::optional<ViewportTranslateAxis> hit_test_translate_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position) {
    if (state.shell_mode != ShellMode::Animation ||
        state.selected_animation_name.empty() || state.weight_paint.enabled ||
        !selected_bone_index(state).has_value() ||
        *selected_bone_index(state) >= layout.bones.size()) {
        return std::nullopt;
    }
    const ImVec2 origin = layout.bones[*selected_bone_index(state)].screen_position;
    if (squared_distance(origin, position) <=
        kTranslateGizmoHitRadius * kTranslateGizmoHitRadius) {
        return ViewportTranslateAxis::Free;
    }
    const ImVec2 x_end(origin.x + kTranslateGizmoLength, origin.y);
    if (point_segment_distance_squared(position, origin, x_end) <=
        kTranslateGizmoHitRadius * kTranslateGizmoHitRadius) {
        return ViewportTranslateAxis::X;
    }
    const ImVec2 y_end(origin.x, origin.y - kTranslateGizmoLength);
    if (point_segment_distance_squared(position, origin, y_end) <=
        kTranslateGizmoHitRadius * kTranslateGizmoHitRadius) {
        return ViewportTranslateAxis::Y;
    }
    return std::nullopt;
}

void draw_translate_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    ImDrawList* draw_list) {
    if (draw_list == nullptr || state.shell_mode != ShellMode::Animation ||
        state.selected_animation_name.empty() ||
        state.weight_paint.enabled || !selected_bone_index(state).has_value() ||
        *selected_bone_index(state) >= layout.bones.size()) {
        return;
    }
    const ImVec2 origin = layout.bones[*selected_bone_index(state)].screen_position;
    const ImVec2 x_end(origin.x + kTranslateGizmoLength, origin.y);
    const ImVec2 y_end(origin.x, origin.y - kTranslateGizmoLength);
    draw_list->AddLine(origin, x_end, IM_COL32(239, 91, 91, 255), 3.0f);
    draw_list->AddTriangleFilled(
        x_end,
        ImVec2(x_end.x - 8.0f, x_end.y - 5.0f),
        ImVec2(x_end.x - 8.0f, x_end.y + 5.0f),
        IM_COL32(239, 91, 91, 255));
    draw_list->AddLine(origin, y_end, IM_COL32(102, 204, 124, 255), 3.0f);
    draw_list->AddTriangleFilled(
        y_end,
        ImVec2(y_end.x - 5.0f, y_end.y + 8.0f),
        ImVec2(y_end.x + 5.0f, y_end.y + 8.0f),
        IM_COL32(102, 204, 124, 255));
    draw_list->AddCircleFilled(origin, 6.0f, IM_COL32(244, 198, 88, 255), 16);
    draw_list->AddCircle(origin, 7.0f, IM_COL32(18, 21, 25, 255), 16, 1.0f);
}

std::optional<marrow::runtime::AttachmentVertex> local_position_for_world_target_impl(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index,
    const ViewportWorldPoint& target) {
    if (bone_index >= skeleton.data()->bones().size() ||
        bone_index >= skeleton.bone_world_transforms().size()) {
        return std::nullopt;
    }
    const auto& bone = skeleton.data()->bones()[bone_index];
    if (!bone.parent_index.has_value()) {
        constexpr double kEpsilon = 1e-8;
        if (std::abs(skeleton.scale_x()) <= kEpsilon ||
            std::abs(skeleton.scale_y()) <= kEpsilon) {
            return std::nullopt;
        }
        return marrow::runtime::AttachmentVertex{
            target.x / skeleton.scale_x(),
            target.y / skeleton.scale_y()};
    }
    const std::size_t parent_index = *bone.parent_index;
    if (parent_index >= skeleton.bone_world_transforms().size()) {
        return std::nullopt;
    }
    const auto parent = skeleton.bone_world_transforms()[parent_index];
    const double a = parent.a;
    const double b = parent.b;
    const double c = parent.c;
    const double d = parent.d;
    const double determinant = (a * d) - (b * c);
    if (std::abs(determinant) <= 1e-8) {
        return std::nullopt;
    }
    const double dx = target.x - parent.world_x;
    const double dy = target.y - parent.world_y;
    return marrow::runtime::AttachmentVertex{
        ((dx * d) - (dy * b)) / determinant,
        ((dy * a) - (dx * c)) / determinant};
}

void finish_viewport_translate_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->viewport_translate_gesture.has_value()) {
        return;
    }
    ViewportTranslateGesture gesture =
        std::move(*state->viewport_translate_gesture);
    state->viewport_translate_gesture.reset();
    if (!commit || !gesture.changed) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        if (!commit) {
            state->status_message = "Cancelled bone move";
        }
        return;
    }
    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message = "Bone move failed";
        return;
    }
    state->error_message.clear();
    state->status_message = "Keyed " + gesture.bone_name + " translation at " +
        format_time_seconds(state->timeline_time_seconds);
}

bool begin_viewport_translate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportTranslateAxis axis,
    const ImVec2& pointer) {
    if (state == nullptr || state->shell_mode != ShellMode::Animation ||
        state->selected_animation_name.empty() ||
        !selected_bone_index(*state).has_value() || state->preview_skeleton == nullptr ||
        state->load_result.project == nullptr || authoring_gesture_active(*state)) {
        return false;
    }
    const std::size_t bone_index = *selected_bone_index(*state);
    if (bone_index >= state->preview_skeleton->bone_world_transforms().size() ||
        bone_index >= state->load_result.skeleton_data->bones().size()) {
        return false;
    }
    state->timeline_playing = false;
    state->session.set_playing(false);
    const std::string bone_name = state->load_result.skeleton_data->bones()[bone_index].name;
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::MoveBone,
        "Move bone " + bone_name,
        "viewport-translate:" + state->selected_animation_name + ":" + bone_name,
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }
    const auto world = state->preview_skeleton->bone_world_transforms()[bone_index];
    ViewportTranslateGesture gesture;
    gesture.bone_index = bone_index;
    gesture.bone_name = bone_name;
    gesture.axis = axis;
    gesture.pointer_start = world_from_screen(layout, pointer);
    gesture.bone_world_start = ViewportWorldPoint{world.world_x, world.world_y};
    gesture.transaction = std::move(transaction);
    state->viewport_translate_gesture.emplace(std::move(gesture));
    return true;
}

bool update_viewport_translate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    if (state == nullptr || !state->viewport_translate_gesture.has_value() ||
        state->preview_skeleton == nullptr) {
        return false;
    }
    auto& gesture = *state->viewport_translate_gesture;
    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    const double delta_x = pointer_world.x - gesture.pointer_start.x;
    const double delta_y = pointer_world.y - gesture.pointer_start.y;
    if (!gesture.changed && std::abs(delta_x) <= 1e-6 && std::abs(delta_y) <= 1e-6) {
        return true;
    }
    ViewportWorldPoint target = gesture.bone_world_start;
    if (gesture.axis != ViewportTranslateAxis::Y) {
        target.x += delta_x;
    }
    if (gesture.axis != ViewportTranslateAxis::X) {
        target.y += delta_y;
    }
    const auto local = local_position_for_world_target_impl(
        *state->preview_skeleton, gesture.bone_index, target);
    if (!local.has_value()) {
        const std::string error =
            "Cannot move a bone through a singular parent transform.";
        finish_viewport_translate_gesture(state, false);
        state->error_message = error;
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        *gesture.transaction.project(),
        *state->session.runtime_data(),
        state->selected_animation_name,
        gesture.bone_name,
        marrow::editor::TransformTimelineChannel::Translate,
        state->timeline_time_seconds,
        marrow::editor::TransformKeyframePatch{
            std::nullopt,
            local->x,
            local->y});
    const marrow::editor::SessionResult refresh = gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_viewport_translate_gesture(state, false);
        state->error_message = error;
        return false;
    }
    gesture.changed = true;
    sync_shell_from_editor_session(state);
    return true;
}

} // namespace

bool begin_viewport_translate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportTranslateAxis axis,
    const ImVec2& pointer) {
    return begin_viewport_translate_gesture(state, layout, axis, pointer);
}

bool update_viewport_translate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    return update_viewport_translate_gesture(state, layout, pointer);
}

void finish_viewport_translate_gesture_for_smoke(ShellState* state, bool commit) {
    finish_viewport_translate_gesture(state, commit);
}

std::optional<marrow::runtime::AttachmentVertex> bone_local_position_from_world(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index,
    const ViewportWorldPoint& target) {
    return local_position_for_world_target_impl(skeleton, bone_index, target);
}


template <typename MutateFn>
bool apply_coalesced_viewport_drag(
    ShellState* state,
    bool changed,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const ImGuiID item_id = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) {
        state->pending_edit_action = PendingEditAction{
            item_id,
            EditActionKind::EditProperty,
            std::move(label),
            std::move(group),
            allow_merge,
            capture_history_snapshot(*state)};
    }

    if (changed) {
        mutate();
        update_project_dirty_state(state);
    }

    if (ImGui::IsItemDeactivatedAfterEdit() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        PendingEditAction pending = std::move(*state->pending_edit_action);
        state->pending_edit_action.reset();
        return record_action_from_snapshots(
            state,
            pending.before_snapshot,
            pending.kind,
            std::move(pending.label),
            std::move(pending.group),
            pending.allow_merge);
    }

    if (ImGui::IsItemDeactivated() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        state->pending_edit_action.reset();
    }

    return true;
}

template <typename MutateFn>
bool apply_coalesced_onion_skin_drag(
    ShellState* state,
    bool changed,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    return apply_coalesced_viewport_drag(
        state,
        changed,
        std::move(label),
        std::move(group),
        allow_merge,
        [&]() {
            auto settings = state->viewport.onion_skin;
            mutate(&settings);
            state->viewport.onion_skin = settings;
            state->load_result.project->editor_metadata.viewport.onion_skin = settings;
        });
}

const char* onion_skin_mode_name(marrow::editor::OnionSkinMode mode) {
    switch (mode) {
    case marrow::editor::OnionSkinMode::Frame:
        return "Frame";
    case marrow::editor::OnionSkinMode::Keyframe:
        return "Keyframe";
    }

    return "Frame";
}


void draw_viewport_pose_fallback(
    const std::vector<BoneCanvasNode>& bones,
    float joint_radius,
    std::optional<std::size_t> selected_bone,
    std::optional<std::size_t> hovered_bone,
    ImU32 active_line_color,
    ImU32 inactive_line_color,
    ImU32 selected_line_color,
    ImU32 active_fill_color,
    ImU32 inactive_fill_color,
    ImU32 hovered_fill_color,
    ImU32 selected_fill_color,
    ImU32 active_outline_color,
    ImU32 inactive_outline_color,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (const BoneCanvasNode& node : bones) {
        if (!node.parent_index.has_value() || *node.parent_index >= bones.size()) {
            continue;
        }

        const BoneCanvasNode& parent = bones[*node.parent_index];
        const bool selected =
            selected_bone.has_value() && *selected_bone == node.bone_index;
        const ImU32 line_color = selected
            ? selected_line_color
            : node.active ? active_line_color : inactive_line_color;
        draw_list->AddLine(
            parent.screen_position,
            node.screen_position,
            line_color,
            selected ? 3.0f : 2.0f);
    }

    for (const BoneCanvasNode& node : bones) {
        const bool selected =
            selected_bone.has_value() && *selected_bone == node.bone_index;
        const bool hovered_selection =
            hovered_bone.has_value() && *hovered_bone == node.bone_index;
        const float radius = joint_radius + (selected ? 2.0f : 0.0f);
        const ImU32 fill_color = selected
            ? selected_fill_color
            : hovered_selection ? hovered_fill_color
                                : node.active ? active_fill_color : inactive_fill_color;
        const ImU32 outline_color =
            node.active ? active_outline_color : inactive_outline_color;
        draw_list->AddCircleFilled(node.screen_position, radius, fill_color, 18);
        draw_list->AddCircle(node.screen_position, radius, outline_color, 18, 1.5f);
    }
}

ImU32 average_overlay_triangle_color(
    const MeshWeightOverlayVertex& a,
    const MeshWeightOverlayVertex& b,
    const MeshWeightOverlayVertex& c) {
    const ImVec4 color_a = mesh_weight_heatmap_color(a.weight);
    const ImVec4 color_b = mesh_weight_heatmap_color(b.weight);
    const ImVec4 color_c = mesh_weight_heatmap_color(c.weight);
    const ImVec4 average(
        (color_a.x + color_b.x + color_c.x) / 3.0f,
        (color_a.y + color_b.y + color_c.y) / 3.0f,
        (color_a.z + color_b.z + color_c.z) / 3.0f,
        (color_a.w + color_b.w + color_c.w) / 3.0f);
    return ImGui::ColorConvertFloat4ToU32(average);
}

void draw_mesh_weight_overlay_fallback(
    const ViewportLayout& layout,
    const MeshWeightOverlay& overlay,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (std::size_t triangle_index = 0; triangle_index + 2U < overlay.triangles.size();
         triangle_index += 3U) {
        const std::size_t a = overlay.triangles[triangle_index];
        const std::size_t b = overlay.triangles[triangle_index + 1U];
        const std::size_t c = overlay.triangles[triangle_index + 2U];
        if (a >= overlay.vertices.size() ||
            b >= overlay.vertices.size() ||
            c >= overlay.vertices.size()) {
            continue;
        }

        draw_list->AddTriangleFilled(
            overlay.vertices[a].screen_position,
            overlay.vertices[b].screen_position,
            overlay.vertices[c].screen_position,
            average_overlay_triangle_color(
                overlay.vertices[a],
                overlay.vertices[b],
                overlay.vertices[c]));
    }

    for (const MeshWeightOverlayVertex& vertex : overlay.vertices) {
        const ImU32 fill_color =
            ImGui::ColorConvertFloat4ToU32(mesh_weight_heatmap_color(vertex.weight, 0.84f));
        draw_list->AddCircleFilled(
            vertex.screen_position,
            std::clamp(layout.render_joint_radius * 0.75f, 4.0f, 8.0f),
            fill_color,
            14);
        draw_list->AddCircle(
            vertex.screen_position,
            std::clamp(layout.render_joint_radius * 0.75f, 4.0f, 8.0f),
            IM_COL32(18, 21, 25, 180),
            14,
            1.0f);
    }
}

void draw_debug_overlay_fallback(
    const DebugOverlayGeometry& overlay,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (const auto& line : overlay.lines) {
        draw_list->AddLine(line.start, line.end, line.color, line.thickness);
    }
    for (const auto& circle : overlay.circles) {
        if ((circle.fill_color & IM_COL32_A_MASK) != 0U) {
            draw_list->AddCircleFilled(circle.center, circle.radius, circle.fill_color, 18);
        }
        if ((circle.outline_color & IM_COL32_A_MASK) != 0U) {
            draw_list->AddCircle(
                circle.center,
                circle.radius,
                circle.outline_color,
                24,
                circle.outline_thickness);
        }
    }
}

void draw_viewport_fallback_scene(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list) {
    draw_list->AddRectFilled(
        layout.canvas_origin,
        layout.canvas_end,
        IM_COL32(18, 21, 25, 255),
        6.0f);
    draw_list->AddRect(
        layout.canvas_origin,
        layout.canvas_end,
        IM_COL32(56, 61, 69, 255),
        6.0f);

    const float grid_spacing = std::max(18.0f, 40.0f * static_cast<float>(state.viewport.zoom));
    for (float x = first_grid_line(layout.world_origin_screen.x, layout.canvas_origin.x, grid_spacing);
         x < layout.canvas_end.x;
         x += grid_spacing) {
        draw_list->AddLine(
            ImVec2(x, layout.canvas_origin.y),
            ImVec2(x, layout.canvas_end.y),
            IM_COL32(31, 35, 41, 255));
    }
    for (float y = first_grid_line(layout.world_origin_screen.y, layout.canvas_origin.y, grid_spacing);
         y < layout.canvas_end.y;
         y += grid_spacing) {
        draw_list->AddLine(
            ImVec2(layout.canvas_origin.x, y),
            ImVec2(layout.canvas_end.x, y),
            IM_COL32(31, 35, 41, 255));
    }

    draw_list->AddLine(
        ImVec2(layout.canvas_origin.x, layout.world_origin_screen.y),
        ImVec2(layout.canvas_end.x, layout.world_origin_screen.y),
        IM_COL32(189, 86, 37, 255),
        1.5f);
    draw_list->AddLine(
        ImVec2(layout.world_origin_screen.x, layout.canvas_origin.y),
        ImVec2(layout.world_origin_screen.x, layout.canvas_end.y),
        IM_COL32(204, 177, 110, 255),
        1.5f);

    for (const OnionSkinGhostPose& ghost_pose : ghost_poses) {
        draw_viewport_pose_fallback(
            ghost_pose.bones,
            layout.render_joint_radius * 0.9f,
            std::nullopt,
            std::nullopt,
            ghost_pose.line_color,
            ghost_pose.line_color,
            ghost_pose.line_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.outline_color,
            ghost_pose.outline_color,
            draw_list);
    }

    if (mesh_weight_overlay != nullptr) {
        draw_mesh_weight_overlay_fallback(layout, *mesh_weight_overlay, draw_list);
    }

    const DebugOverlayGeometry debug_overlay = build_debug_overlay_geometry(state, layout);
    draw_debug_overlay_fallback(debug_overlay, draw_list);

    if (state.viewport.debug_overlay.bones) {
        draw_viewport_pose_fallback(
            layout.bones,
            layout.render_joint_radius,
            selected_bone_index(state),
            hovered_bone,
            IM_COL32(214, 163, 76, 220),
            IM_COL32(111, 117, 125, 180),
            IM_COL32(247, 204, 114, 255),
            IM_COL32(208, 134, 57, 230),
            IM_COL32(98, 103, 110, 200),
            IM_COL32(226, 186, 97, 240),
            IM_COL32(247, 204, 114, 255),
            IM_COL32(33, 37, 41, 255),
            IM_COL32(48, 50, 54, 255),
            draw_list);
    }
}

void draw_viewport_annotations(
    const ShellState& state,
    const ViewportLayout& layout,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list) {
    if (!state.load_result) {
        return;
    }

    const auto& bones = state.load_result.skeleton_data->bones();
    if (state.viewport.debug_overlay.bones) {
        for (const BoneCanvasNode& node : layout.bones) {
            const bool selected =
                selected_bone_index(state).has_value() && *selected_bone_index(state) == node.bone_index;
            const bool hovered_selection =
                hovered_bone.has_value() && *hovered_bone == node.bone_index;
            const float radius = layout.render_joint_radius + (selected ? 2.0f : 0.0f);
            if (selected || hovered_selection || layout.bones.size() <= 12U) {
                draw_list->AddText(
                    ImVec2(node.screen_position.x + radius + 6.0f, node.screen_position.y - 6.0f),
                    selected ? IM_COL32(247, 232, 191, 255) : IM_COL32(225, 212, 180, 220),
                    bones[node.bone_index].name.c_str());
            }
        }
    }

    // ── Glass selection chip (top-left) ──
    if (selected_bone_index(state).has_value() &&
        *selected_bone_index(state) < bones.size()) {
        std::ostringstream selection_stream;
        selection_stream << bones[*selected_bone_index(state)].name
                         << "  ·  pan(" << static_cast<int>(state.viewport.pan_x) << ", "
                         << static_cast<int>(state.viewport.pan_y) << ")"
                         << "  ·  zoom " << state.viewport.zoom;
        const std::string chip_text = selection_stream.str();
        const ImVec2 text_size = ImGui::CalcTextSize(chip_text.c_str());
        const float pad_x = 10.0f;
        const float pad_y = 6.0f;
        const ImVec2 chip_min(
            layout.canvas_origin.x + 10.0f,
            layout.canvas_origin.y + 10.0f);
        const ImVec2 chip_max(
            chip_min.x + text_size.x + pad_x * 2.0f,
            chip_min.y + text_size.y + pad_y * 2.0f);
        // Glass background: surface-highest @ 85% opacity
        draw_list->AddRectFilled(
            chip_min,
            chip_max,
            IM_COL32(0x32, 0x35, 0x3b, 0xD9),
            2.0f);
        // Ghost border: outline-variant @ 20% opacity
        draw_list->AddRect(
            chip_min,
            chip_max,
            IM_COL32(0x43, 0x46, 0x54, 0x33),
            2.0f,
            0,
            1.0f);
        draw_list->AddText(
            ImVec2(chip_min.x + pad_x, chip_min.y + pad_y),
            IM_COL32(0xe1, 0xe2, 0xea, 0xFF),
            chip_text.c_str());
    }

    const DebugOverlayGeometry debug_overlay = build_debug_overlay_geometry(state, layout);
    std::vector<const char*> enabled_layers;
    enabled_layers.reserve(6U);
    if (state.viewport.debug_overlay.bones) {
        enabled_layers.push_back("bones");
    }
    if (debug_overlay.stats.ik_constraint_count > 0U) {
        enabled_layers.push_back("ik");
    }
    if (debug_overlay.stats.path_constraint_count > 0U) {
        enabled_layers.push_back("path");
    }
    if (debug_overlay.stats.physics_constraint_count > 0U) {
        enabled_layers.push_back("physics");
    }
    if (debug_overlay.stats.mesh_attachment_count > 0U) {
        enabled_layers.push_back("meshes");
    }
    if (debug_overlay.stats.bounding_box_count > 0U) {
        enabled_layers.push_back("bounds");
    }
    if (!enabled_layers.empty()) {
        std::ostringstream debug_stream;
        debug_stream << "Debug overlay:";
        for (const char* layer : enabled_layers) {
            debug_stream << ' ' << layer;
        }
        draw_list->AddText(
            ImVec2(layout.canvas_origin.x + 14.0f, layout.canvas_origin.y + 30.0f),
            IM_COL32(210, 221, 232, 220),
            debug_stream.str().c_str());
    }

    // ── Glass perf HUD (top-right) ──
    if (state.hud_overlay_enabled && state.hud_overlay_frame.has_value()) {
        const std::vector<std::string> hud_lines =
            marrow::runtime::profiler_hud_lines(*state.hud_overlay_frame);
        const float line_height = ImGui::GetTextLineHeight();
        const float line_spacing = line_height + 2.0f;
        const float pad_x = 10.0f;
        const float pad_y = 6.0f;
        const char* title = "PERF HUD";
        const ImVec2 title_size = ImGui::CalcTextSize(title);
        float max_line_width = title_size.x;
        for (const std::string& line : hud_lines) {
            const ImVec2 s = ImGui::CalcTextSize(line.c_str());
            max_line_width = std::max(max_line_width, s.x);
        }
        const float title_block = title_size.y + 4.0f + 1.0f;  // title + gap + divider
        const float body_block = static_cast<float>(hud_lines.size()) * line_spacing;
        const float chip_w = max_line_width + pad_x * 2.0f;
        const float chip_h = title_block + 4.0f + body_block + pad_y * 2.0f;
        const ImVec2 chip_max(layout.canvas_end.x - 10.0f, layout.canvas_origin.y + 10.0f + chip_h);
        const ImVec2 chip_min(chip_max.x - chip_w, layout.canvas_origin.y + 10.0f);
        draw_list->AddRectFilled(
            chip_min,
            chip_max,
            IM_COL32(0x32, 0x35, 0x3b, 0xD9),
            2.0f);
        draw_list->AddRect(
            chip_min,
            chip_max,
            IM_COL32(0x43, 0x46, 0x54, 0x33),
            2.0f,
            0,
            1.0f);
        // Title
        draw_list->AddText(
            ImVec2(chip_min.x + pad_x, chip_min.y + pad_y),
            IM_COL32(0x8d, 0x90, 0xa0, 0xFF),
            title);
        // Divider line
        const float divider_y = chip_min.y + pad_y + title_size.y + 3.0f;
        draw_list->AddLine(
            ImVec2(chip_min.x + pad_x, divider_y),
            ImVec2(chip_max.x - pad_x, divider_y),
            IM_COL32(0x43, 0x46, 0x54, 0x66),
            1.0f);
        // Body lines
        float line_y = divider_y + 4.0f;
        for (const std::string& line : hud_lines) {
            draw_list->AddText(
                ImVec2(chip_min.x + pad_x, line_y),
                IM_COL32(0xc3, 0xc6, 0xd6, 0xFF),
                line.c_str());
            line_y += line_spacing;
        }
    }

    // ── Glass onion skin legend (bottom-left) ──
    if (state.viewport.onion_skin.enabled) {
        const char* legend_text = "Onion skin  ·  prev  ·  next";
        const ImVec2 text_size = ImGui::CalcTextSize(legend_text);
        const float pad_x = 10.0f;
        const float pad_y = 6.0f;
        const ImVec2 chip_min(
            layout.canvas_origin.x + 10.0f,
            layout.canvas_end.y - 10.0f - (text_size.y + pad_y * 2.0f));
        const ImVec2 chip_max(
            chip_min.x + text_size.x + pad_x * 2.0f,
            layout.canvas_end.y - 10.0f);
        draw_list->AddRectFilled(
            chip_min,
            chip_max,
            IM_COL32(0x32, 0x35, 0x3b, 0xD9),
            2.0f);
        draw_list->AddRect(
            chip_min,
            chip_max,
            IM_COL32(0x43, 0x46, 0x54, 0x33),
            2.0f,
            0,
            1.0f);
        draw_list->AddText(
            ImVec2(chip_min.x + pad_x, chip_min.y + pad_y),
            IM_COL32(0xc3, 0xc6, 0xd6, 0xFF),
            legend_text);
        // Colored dots for prev (primary) and next (tertiary)
        const float dot_y = chip_min.y + pad_y + text_size.y * 0.5f;
        const ImVec2 prev_size = ImGui::CalcTextSize("prev  ·  next");
        const float prev_dot_x = chip_max.x - pad_x - prev_size.x - 8.0f;
        draw_list->AddCircleFilled(
            ImVec2(prev_dot_x, dot_y),
            3.0f,
            IM_COL32(0xb3, 0xc5, 0xff, 0x80));  // primary @ 50%
        const ImVec2 next_size = ImGui::CalcTextSize("next");
        const float next_dot_x = chip_max.x - pad_x - next_size.x - 8.0f;
        draw_list->AddCircleFilled(
            ImVec2(next_dot_x, dot_y),
            3.0f,
            IM_COL32(0xff, 0xb3, 0xad, 0x80));  // tertiary @ 50%
    }

    if (mesh_weight_overlay != nullptr) {
        std::ostringstream overlay_stream;
        overlay_stream << "Weight heatmap: "
                       << mesh_weight_overlay->target.display_attachment_name
                       << " -> " << mesh_weight_overlay->target.source_skin_name
                       << "/" << mesh_weight_overlay->target.source_attachment_name
                       << "  " << weight_paint_mode_name(state.weight_paint.mode)
                       << "  radius " << static_cast<int>(state.weight_paint.radius_pixels)
                       << "  strength " << std::fixed << std::setprecision(2)
                       << state.weight_paint.strength;
        draw_list->AddText(
            ImVec2(layout.canvas_origin.x + 14.0f, layout.canvas_end.y - 48.0f),
            IM_COL32(233, 223, 199, 230),
            overlay_stream.str().c_str());
    }
}


void draw_viewport_window(ShellState* state) {
    ImGui::Begin(kViewportWindowTitle);

    // No scene → editorial empty state, no chrome.
    if (!state->load_result) {
        widgets::empty_hero(
            "VIEWPORT",
            "Nothing staged",
            "Open a .marrow project to preview the rig. The viewport "
            "renders the runtime skeleton, onion skins and weight heatmaps.");
        ImGui::End();
        return;
    }

    const std::optional<MeshWeightPaintTarget> paint_target =
        state->load_result ? current_mesh_weight_paint_target(*state) : std::nullopt;
    const bool weight_tool_ready =
        state->weight_paint.enabled &&
        paint_target.has_value() &&
        selected_bone_index(*state).has_value() &&
        state->load_result &&
        *selected_bone_index(*state) < state->load_result.skeleton_data->bones().size();
    const std::string preview_label =
        state->selected_animation_name.empty() ? std::string("Setup pose preview")
                                               : "Animation preview / " + state->selected_animation_name;

    // ── Viewport Toolbar ──
    if (state->load_result && state->preview_skeleton) {
        const ImVec2 pre_toolbar_avail = ImGui::GetContentRegionAvail();
        if (icon_button(state->icons, Icon::ZoomFit, "Zoom to fit")) {
            auto_frame_skeleton(state, pre_toolbar_avail);
        }
        ImGui::SameLine();
        if (icon_button(state->icons, Icon::ZoomOne, "Zoom 1:1")) {
            state->viewport.zoom = 1.0;
            state->viewport.pan_x = 0.0;
            state->viewport.pan_y = 0.0;
            state->status_message = "Reset viewport to 1:1";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        {
            const bool bones_on = state->viewport.debug_overlay.bones;
            if (icon_button(state->icons, Icon::BoneToggle, "Toggle bone overlay", bones_on)) {
                apply_debug_overlay_edit(
                    state,
                    std::string(bones_on ? "Disabled" : "Enabled") + " bones",
                    "viewport:debug-overlay:bones",
                    false,
                    [bones_on](marrow::editor::DebugOverlaySettings* s) {
                        s->bones = !bones_on;
                    });
            }
        }
        ImGui::SameLine();
        {
            const bool onion_on = state->viewport.onion_skin.enabled;
            if (icon_button(state->icons, Icon::OnionSkin, "Toggle onion skin", onion_on)) {
                apply_onion_skin_edit(
                    state,
                    std::string(onion_on ? "Disabled" : "Enabled") + " onion skinning",
                    "viewport:onion-skin:enabled",
                    false,
                    [onion_on](marrow::editor::OnionSkinSettings* s) {
                        s->enabled = !onion_on;
                    });
            }
        }
        ImGui::SameLine();
        {
            const bool hud_on = state->hud_overlay_enabled;
            if (icon_button(state->icons, Icon::PerfHud, "Toggle performance HUD", hud_on)) {
                state->hud_overlay_enabled = !hud_on;
                if (hud_on) {
                    state->hud_overlay_frame.reset();
                }
            }
        }
    }

    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 32.0f || canvas_size.y < 32.0f) {
        ImGui::End();
        return;
    }

    const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
    const ViewportFramebufferSize framebuffer_size = viewport_framebuffer_size(
        canvas_size,
        ImGui::GetIO().DisplayFramebufferScale);
    bool use_framebuffer = state->viewport_renderer.available;
    if (use_framebuffer) {
        if (const auto error = ensure_viewport_framebuffer(
                &state->viewport_renderer,
                framebuffer_size.width,
                framebuffer_size.height)) {
            state->viewport_renderer.error_message = *error;
            use_framebuffer = false;
        }
    }

    if (use_framebuffer) {
        ImGui::Image(
            ImTextureRef(static_cast<ImTextureID>(state->viewport_renderer.color_texture)),
            canvas_size,
            kViewportImageUv0,
            kViewportImageUv1);
        ImGui::SetCursorScreenPos(canvas_origin);
    }
    ImGui::InvisibleButton(
        "viewport_canvas",
        canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (!state->viewport_translate_gesture.has_value() && hovered &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
        const ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
        state->viewport.pan_x += static_cast<double>(mouse_delta.x);
        state->viewport.pan_y += static_cast<double>(mouse_delta.y);
    }

    if (!state->viewport_translate_gesture.has_value() && hovered &&
        std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
        const float zoom_factor = ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : 0.9f;
        (void)zoom_viewport_at_screen_position(
            state,
            canvas_origin,
            canvas_size,
            ImGui::GetIO().MousePos,
            static_cast<double>(zoom_factor));
    }

    const auto layout = build_viewport_layout(*state, canvas_origin, canvas_size);
    state->hud_overlay_frame =
        state->hud_overlay_enabled ? build_preview_profiler_frame(*state) : std::nullopt;
    std::optional<MeshWeightOverlay> mesh_weight_overlay =
        layout.has_value() && (state->weight_paint.enabled || state->weight_paint.show_heatmap)
            ? build_mesh_weight_overlay(*state, *layout)
            : std::nullopt;
    const bool brush_enabled =
        state->weight_paint.enabled &&
        mesh_weight_overlay.has_value() &&
        selected_bone_index(*state).has_value() &&
        state->load_result &&
        *selected_bone_index(*state) < state->load_result.skeleton_data->bones().size();
    std::optional<std::size_t> hovered_bone;
    if (hovered && layout.has_value()) {
        hovered_bone = pick_bone_at_position(*layout, ImGui::GetIO().MousePos);
    }
    const std::optional<ViewportTranslateAxis> hovered_translate_axis =
        hovered && layout.has_value()
            ? hit_test_translate_gizmo(*state, *layout, ImGui::GetIO().MousePos)
            : std::nullopt;
    const std::vector<OnionSkinGhostPose> ghost_poses =
        layout.has_value() ? build_onion_skin_ghost_poses(*state, *layout)
                           : std::vector<OnionSkinGhostPose>{};
    if (state->weight_paint_stroke.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        finish_weight_paint_stroke(state);
    }
    if (!brush_enabled && hovered && layout.has_value() &&
        hovered_translate_axis.has_value() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        (void)begin_viewport_translate_gesture(
            state,
            *layout,
            *hovered_translate_axis,
            ImGui::GetIO().MousePos);
    }
    if (state->viewport_translate_gesture.has_value() && layout.has_value() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        (void)update_viewport_translate_gesture(
            state, *layout, ImGui::GetIO().MousePos);
    }
    if (brush_enabled &&
        hovered &&
        mesh_weight_overlay.has_value() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        begin_weight_paint_stroke(state, mesh_weight_overlay->target);
    }
    if (brush_enabled &&
        hovered &&
        mesh_weight_overlay.has_value() &&
        state->weight_paint_stroke.active &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float sample_spacing = std::max(state->weight_paint.radius_pixels * 0.18f, 6.0f);
        if (!state->weight_paint_stroke.has_last_sample ||
            std::sqrt(
                squared_distance(
                    ImGui::GetIO().MousePos,
                    state->weight_paint_stroke.last_sample_position)) >= sample_spacing) {
            if (apply_weight_paint_sample(state, *mesh_weight_overlay, ImGui::GetIO().MousePos) &&
                layout.has_value()) {
                state->weight_paint_stroke.last_sample_position = ImGui::GetIO().MousePos;
                state->weight_paint_stroke.has_last_sample = true;
                mesh_weight_overlay = build_mesh_weight_overlay(*state, *layout);
            }
        }
    }
    if (!brush_enabled && !state->viewport_translate_gesture.has_value() &&
        hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        hovered_bone.has_value()) {
        select_bone(state, *hovered_bone, "Viewport", true);
    }

    const ImVec2 canvas_end(
        canvas_origin.x + canvas_size.x,
        canvas_origin.y + canvas_size.y);
    const MeshWeightOverlay* rendered_overlay =
        state->weight_paint.show_heatmap && mesh_weight_overlay.has_value()
            ? &(*mesh_weight_overlay)
            : nullptr;
    if (use_framebuffer && layout.has_value()) {
        if (const auto error = render_viewport_framebuffer(
                *state,
                *layout,
                ghost_poses,
                hovered_bone,
                rendered_overlay,
                &state->viewport_renderer)) {
            state->viewport_renderer.error_message = *error;
            use_framebuffer = false;
        } else {
            state->viewport_renderer.error_message.clear();
        }
    }

    if (use_framebuffer) {
        draw_list->AddRect(canvas_origin, canvas_end, IM_COL32(56, 61, 69, 255), 6.0f);
    } else {
        if (layout.has_value()) {
            draw_viewport_fallback_scene(
                *state,
                *layout,
                ghost_poses,
                hovered_bone,
                rendered_overlay,
                draw_list);
        } else {
            draw_list->AddRectFilled(
                canvas_origin,
                canvas_end,
                IM_COL32(18, 21, 25, 255),
                6.0f);
            draw_list->AddRect(
                canvas_origin,
                canvas_end,
                IM_COL32(56, 61, 69, 255),
                6.0f);
        }
    }

    if (layout.has_value()) {
        draw_viewport_annotations(*state, *layout, hovered_bone, rendered_overlay, draw_list);
        draw_translate_gizmo(*state, *layout, draw_list);
        if (brush_enabled && hovered) {
            draw_list->AddCircle(
                ImGui::GetIO().MousePos,
                state->weight_paint.radius_pixels,
                IM_COL32(248, 236, 211, 210),
                48,
                1.5f);
        }
    } else {
        draw_list->AddText(
            ImVec2(canvas_origin.x + 16.0f, canvas_origin.y + 16.0f),
            IM_COL32(240, 232, 213, 255),
            "Project load failed. Reload a valid .marrow file.");
    }

    // ── Floating translucent header (top-left, overlaid on the canvas) ──
    {
        const bool animation_move_ready =
            !weight_tool_ready && !state->selected_animation_name.empty() &&
            selected_bone_index(*state).has_value();
        std::string hint =
            std::string(weight_tool_ready
                            ? "LMB brush weights"
                            : animation_move_ready ? "LMB move gizmo / select" : "LMB select") +
            "   ·   RMB pan   ·   Wheel zoom";
        if (!state->selected_animation_name.empty()) {
            hint += "   ·   " +
                    format_time_seconds(state->timeline_time_seconds) + " / " +
                    format_time_seconds(timeline_preview_duration(*state));
        }
        const ImVec2 title_size = ImGui::CalcTextSize(preview_label.c_str());
        const ImVec2 hint_size = ImGui::CalcTextSize(hint.c_str());
        const float pad_x = 12.0f;
        const float pad_y = 8.0f;
        const float gap_y = 4.0f;
        const float body_w = std::max(title_size.x, hint_size.x);
        const ImVec2 head_min(canvas_origin.x + 10.0f, canvas_origin.y + 10.0f);
        const ImVec2 head_max(
            head_min.x + body_w + pad_x * 2.0f,
            head_min.y + title_size.y + hint_size.y + gap_y + pad_y * 2.0f);
        draw_list->AddRectFilled(
            head_min, head_max, IM_COL32(0x32, 0x35, 0x3b, 0xD9), 4.0f);
        draw_list->AddRect(
            head_min, head_max, IM_COL32(0x43, 0x46, 0x54, 0x33), 4.0f, 0, 1.0f);
        draw_list->AddText(
            ImVec2(head_min.x + pad_x, head_min.y + pad_y),
            IM_COL32(0xe1, 0xe2, 0xea, 0xFF),
            preview_label.c_str());
        draw_list->AddText(
            ImVec2(head_min.x + pad_x,
                   head_min.y + pad_y + title_size.y + gap_y),
            IM_COL32(0x9a, 0x9e, 0xab, 0xFF),
            hint.c_str());
    }

    if (hovered_bone.has_value() && state->load_result) {
        const auto& bones = state->load_result.skeleton_data->bones();
        ImGui::SetTooltip("%s", bones[*hovered_bone].name.c_str());
    }

    if (!state->viewport_renderer.error_message.empty()) {
        draw_list->AddText(
            ImVec2(canvas_origin.x + 14.0f, canvas_end.y - 28.0f),
            IM_COL32(228, 143, 104, 255),
            state->viewport_renderer.error_message.c_str());
    }

    ImGui::End();
}

void finalize_orphaned_viewport_translate_gesture(ShellState* state) {
    if (state == nullptr || !state->viewport_translate_gesture.has_value()) {
        return;
    }
    const bool cancel = ImGui::GetCurrentContext() == nullptr ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (cancel) {
        finish_viewport_translate_gesture(state, false);
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        finish_viewport_translate_gesture(state, true);
    }
}

void draw_viewport_settings(ShellState* state) {
    if (!state->load_result || !state->load_result.project) {
        return;
    }

    const std::optional<MeshWeightPaintTarget> paint_target =
        state->load_result ? current_mesh_weight_paint_target(*state) : std::nullopt;

    if (ImGui::CollapsingHeader("Onion Skin##settings")) {
        const auto& onion_skin = state->viewport.onion_skin;
        bool onion_enabled = onion_skin.enabled;
        if (ImGui::Checkbox("Enabled##onion_skin", &onion_enabled)) {
            apply_onion_skin_edit(
                state,
                std::string(onion_enabled ? "Enabled" : "Disabled") + " onion skinning",
                "viewport:onion-skin:enabled",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->enabled = onion_enabled;
                });
        }

        int mode_index = onion_skin.mode == marrow::editor::OnionSkinMode::Frame ? 0 : 1;
        constexpr const char* kOnionSkinModes[] = {"Frame", "Keyframe"};
        if (ImGui::Combo(
                "Mode##onion_skin",
                &mode_index,
                kOnionSkinModes,
                IM_ARRAYSIZE(kOnionSkinModes))) {
            apply_onion_skin_edit(
                state,
                std::string("Set onion skin mode to ") + kOnionSkinModes[mode_index],
                "viewport:onion-skin:mode",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->mode = mode_index == 0 ? marrow::editor::OnionSkinMode::Frame
                                                     : marrow::editor::OnionSkinMode::Keyframe;
                });
        }

        bool anchor_to_zero = onion_skin.anchor_to_zero;
        if (mode_index != 0) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Checkbox("Anchor To Frame 0##onion_skin", &anchor_to_zero)) {
            apply_onion_skin_edit(
                state,
                std::string(anchor_to_zero ? "Enabled" : "Disabled") + " onion-skin anchoring",
                "viewport:onion-skin:anchor",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->anchor_to_zero = anchor_to_zero;
                });
        }
        if (mode_index != 0) {
            ImGui::EndDisabled();
        }

        int before_count = onion_skin.before_count;
        const bool before_changed =
            ImGui::SliderInt("Before Ghosts##onion_skin", &before_count, 0, 6);
        apply_coalesced_onion_skin_drag(
            state,
            before_changed,
            "Updated onion-skin before ghost count",
            "viewport:onion-skin:before",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->before_count = before_count;
            });

        int after_count = onion_skin.after_count;
        const bool after_changed =
            ImGui::SliderInt("After Ghosts##onion_skin", &after_count, 0, 6);
        apply_coalesced_onion_skin_drag(
            state,
            after_changed,
            "Updated onion-skin after ghost count",
            "viewport:onion-skin:after",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->after_count = after_count;
            });

        int step = onion_skin.step;
        const char* step_label = mode_index == 0 ? "Frame Step##onion_skin"
                                                 : "Keyframe Stride##onion_skin";
        const bool step_changed = ImGui::SliderInt(step_label, &step, 1, 12);
        apply_coalesced_onion_skin_drag(
            state,
            step_changed,
            "Updated onion-skin sampling step",
            "viewport:onion-skin:step",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->step = step;
            });

        ImGui::TextDisabled(
            "Before ghosts render cool blue, after ghosts render warm red.");
    }

    if (ImGui::CollapsingHeader("Debug Overlay##settings")) {
        const auto& debug_overlay = state->viewport.debug_overlay;
        const auto draw_toggle = [&](const char* label,
                                     const char* status_label,
                                     bool value,
                                     std::string_view group,
                                     auto mutate) {
            bool edited = value;
            if (ImGui::Checkbox(label, &edited)) {
                apply_debug_overlay_edit(
                    state,
                    std::string(edited ? "Enabled " : "Disabled ") + status_label,
                    std::string(group),
                    false,
                    [&](marrow::editor::DebugOverlaySettings* settings) {
                        mutate(settings, edited);
                    });
            }
        };

        draw_toggle(
            "Bones##debug_overlay", "bones",
            debug_overlay.bones, "viewport:debug-overlay:bones",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->bones = v; });
        draw_toggle(
            "IK Constraints##debug_overlay", "IK constraints",
            debug_overlay.ik_constraints, "viewport:debug-overlay:ik",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->ik_constraints = v; });
        draw_toggle(
            "Path Constraints##debug_overlay", "path constraints",
            debug_overlay.path_constraints, "viewport:debug-overlay:path",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->path_constraints = v; });
        draw_toggle(
            "Physics Constraints##debug_overlay", "physics constraints",
            debug_overlay.physics_constraints, "viewport:debug-overlay:physics",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->physics_constraints = v; });
        draw_toggle(
            "Mesh Wireframes##debug_overlay", "mesh wireframes",
            debug_overlay.mesh_wireframes, "viewport:debug-overlay:meshes",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->mesh_wireframes = v; });
        draw_toggle(
            "Bounding Boxes##debug_overlay", "bounding boxes",
            debug_overlay.bounding_boxes, "viewport:debug-overlay:bounds",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->bounding_boxes = v; });
    }

    if (ImGui::CollapsingHeader("Performance HUD##settings")) {
        bool hud_enabled = state->hud_overlay_enabled;
        if (ImGui::Checkbox("Enabled##performance_hud", &hud_enabled)) {
            state->hud_overlay_enabled = hud_enabled;
            if (!hud_enabled) {
                state->hud_overlay_frame.reset();
            }
        }
        ImGui::TextDisabled("Profiles animation, transforms, skinning, and render prep.");
    }

    if (ImGui::CollapsingHeader("Weight Paint##settings")) {
        bool tool_enabled = state->weight_paint.enabled;
        if (ImGui::Checkbox("Enable Tool##weight_paint", &tool_enabled)) {
            if (!tool_enabled) {
                finish_weight_paint_stroke(state);
            }
            apply_shell_mode(
                state,
                tool_enabled ? ShellMode::WeightPaint : ShellMode::Animation);
        }

        // 3-icon toggle group (Paint / Erase / Smooth)
        const bool mode_paint = state->weight_paint.mode == WeightPaintMode::Paint;
        const bool mode_erase = state->weight_paint.mode == WeightPaintMode::Erase;
        const bool mode_smooth = state->weight_paint.mode == WeightPaintMode::Smooth;
        ImGui::TextDisabled("Mode");
        ImGui::SameLine(0.0f, 10.0f);
        if (icon_button(state->icons, Icon::WeightBrush, "Paint weights", mode_paint)) {
            state->weight_paint.mode = WeightPaintMode::Paint;
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (icon_button(state->icons, Icon::WeightErase, "Erase weights", mode_erase)) {
            state->weight_paint.mode = WeightPaintMode::Erase;
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (icon_button(state->icons, Icon::WeightSmooth, "Smooth weights", mode_smooth)) {
            state->weight_paint.mode = WeightPaintMode::Smooth;
        }

        ImGui::SliderFloat(
            "Radius##weight_paint", &state->weight_paint.radius_pixels,
            8.0f, 160.0f, "%.0f px");
        ImGui::SliderFloat(
            "Strength##weight_paint", &state->weight_paint.strength,
            0.05f, 1.0f, "%.2f");
        ImGui::Checkbox("Show Heat Map##weight_paint", &state->weight_paint.show_heatmap);

        if (paint_target.has_value()) {
            ImGui::Text("Preview mesh: %s", paint_target->display_attachment_name.c_str());
            ImGui::Text("Editing source: %s / %s",
                paint_target->source_skin_name.c_str(),
                paint_target->source_attachment_name.c_str());
        } else {
            ImGui::TextDisabled("Select a mesh slot to paint weights.");
        }

        if (selected_bone_index(*state).has_value() &&
            *selected_bone_index(*state) < state->load_result.skeleton_data->bones().size()) {
            ImGui::Text("Active bone: %s",
                state->load_result.skeleton_data->bones()[*selected_bone_index(*state)].name.c_str());
        } else {
            ImGui::TextDisabled("Select a bone for paint target.");
        }
    }
}


} // namespace marrow::editor::shell
