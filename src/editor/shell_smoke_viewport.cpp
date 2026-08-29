#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "shell_constraints.hpp"
#include "shell_asset_watch.hpp"
#include "shell_agent_panel.hpp"
#include "shell_coalesced_edit.hpp"
#include "shell_derived_cache.hpp"
#include "shell_inspector.hpp"
#include "shell_project_panels.hpp"
#include "shell_parameters.hpp"
#include "shell_smoke_scenarios.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "shell_viewport_ui.hpp"
#include "shell_state.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/module.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"

namespace marrow::editor::shell {

const marrow::renderer::RegionAttachmentDrawCommand* find_region_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& command : scene.draw_commands) {
        const auto* attachment = marrow::renderer::region_attachment_command(command);
        if (attachment != nullptr && attachment->slot_name == slot_name) {
            return attachment;
        }
    }
    return nullptr;
}

std::optional<std::size_t> find_region_attachment_index(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (std::size_t draw_index = 0; draw_index < scene.draw_commands.size(); ++draw_index) {
        const auto* attachment =
            marrow::renderer::region_attachment_command(scene.draw_commands[draw_index]);
        if (attachment != nullptr && attachment->slot_name == slot_name) {
            return draw_index;
        }
    }
    return std::nullopt;
}

const marrow::renderer::DynamicMeshDrawCommand* find_dynamic_mesh_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& command : scene.draw_commands) {
        const auto* attachment = marrow::renderer::dynamic_mesh_attachment_command(command);
        if (attachment != nullptr && attachment->slot_name == slot_name) {
            return attachment;
        }
    }
    return nullptr;
}

const marrow::renderer::ClipAttachmentDrawCommand* find_clip_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view attachment_name) {
    const auto iterator = std::find_if(
        scene.clip_attachments.begin(),
        scene.clip_attachments.end(),
        [&](const marrow::renderer::ClipAttachmentDrawCommand& attachment) {
            return attachment.attachment_name == attachment_name;
        });
    return iterator != scene.clip_attachments.end() ? &(*iterator) : nullptr;
}

bool validate_viewport_camera_smoke(
    const std::filesystem::path& project_path) {
    ShellState camera_state;
    camera_state.project_path = project_path;
    if (!reload_project(&camera_state)) {
        std::cerr << camera_state.error_message << '\n';
        return false;
    }
    if (!camera_state.viewport_camera.initialized ||
        camera_state.preview_skeleton == nullptr ||
        camera_state.preview_skeleton->bone_poses().empty()) {
        std::cerr << "Viewport camera smoke did not initialize from the loaded preview pose.\n";
        return false;
    }
    const auto& persisted_viewport =
        camera_state.load_result.project->editor_metadata.viewport;
    if (camera_state.viewport.pan_x != persisted_viewport.pan_x ||
        camera_state.viewport.pan_y != persisted_viewport.pan_y ||
        camera_state.viewport.zoom != persisted_viewport.zoom) {
        std::cerr << "Viewport camera initialization did not preserve project pan/zoom metadata.\n";
        return false;
    }

    camera_state.viewport.pan_x = 37.25;
    camera_state.viewport.pan_y = -24.5;
    camera_state.viewport.zoom = 1.35;
    const ImVec2 canvas_origin(23.0f, 41.0f);
    const ImVec2 canvas_size(1000.0f, 700.0f);
    const auto initial_layout = build_viewport_layout(
        camera_state,
        canvas_origin,
        canvas_size);
    if (!initial_layout.has_value()) {
        std::cerr << "Viewport camera smoke could not build its initial layout.\n";
        return false;
    }

    const ViewportWorldPoint expected_world{
        initial_layout->world_center_x + 17.125,
        initial_layout->world_center_y - 9.75};
    const ImVec2 expected_screen = screen_from_world(
        *initial_layout,
        expected_world.x,
        expected_world.y);
    const ViewportWorldPoint round_trip_world =
        world_from_screen(*initial_layout, expected_screen);
    if (std::abs(round_trip_world.x - expected_world.x) > 1e-4 ||
        std::abs(round_trip_world.y - expected_world.y) > 1e-4) {
        std::cerr << "Viewport world/screen transforms did not round-trip.\n";
        return false;
    }

    marrow::runtime::Skeleton drag_skeleton(camera_state.load_result.skeleton_data);
    const auto arm_index = drag_skeleton.data()->find_bone_index("arm_l");
    if (!arm_index.has_value()) {
        std::cerr << "Viewport translate smoke could not find arm_l.\n";
        return false;
    }
    const auto parent_index = drag_skeleton.data()->bones()[*arm_index].parent_index;
    if (!parent_index.has_value()) {
        std::cerr << "Viewport translate smoke expected arm_l to have a parent.\n";
        return false;
    }
    auto& parent_pose = drag_skeleton.bone_poses()[*parent_index].local_pose;
    parent_pose.rotation = 31.0f;
    parent_pose.scale_x = 1.7f;
    parent_pose.scale_y = 0.65f;
    parent_pose.shear_x = 8.0f;
    drag_skeleton.update_world_transforms();
    const auto arm_world_before = drag_skeleton.bone_world_transforms()[*arm_index];
    const ViewportWorldPoint arm_target{
        static_cast<double>(arm_world_before.world_x) + 19.0,
        static_cast<double>(arm_world_before.world_y) - 11.0};
    const auto arm_local = bone_local_position_from_world(
        drag_skeleton, *arm_index, arm_target);
    if (!arm_local.has_value()) {
        std::cerr << "Viewport translate smoke could not invert a non-uniform parent.\n";
        return false;
    }
    drag_skeleton.bone_poses()[*arm_index].local_pose.x =
        static_cast<float>(arm_local->x);
    drag_skeleton.bone_poses()[*arm_index].local_pose.y =
        static_cast<float>(arm_local->y);
    drag_skeleton.update_world_transforms();
    const auto arm_world_after = drag_skeleton.bone_world_transforms()[*arm_index];
    if (std::abs(static_cast<double>(arm_world_after.world_x) - arm_target.x) > 1e-3 ||
        std::abs(static_cast<double>(arm_world_after.world_y) - arm_target.y) > 1e-3) {
        std::cerr << "Viewport translate parent inverse did not reach the world target.\n";
        return false;
    }
    parent_pose.scale_x = 0.0f;
    parent_pose.scale_y = 0.0f;
    drag_skeleton.update_world_transforms();
    if (bone_local_position_from_world(drag_skeleton, *arm_index, arm_target).has_value()) {
        std::cerr << "Viewport translate did not reject a singular parent transform.\n";
        return false;
    }

    marrow::runtime::Skeleton rotation_skeleton(camera_state.load_result.skeleton_data);
    const auto rotation_root = rotation_skeleton.data()->find_bone_index("root");
    const auto rotation_arm = rotation_skeleton.data()->find_bone_index("arm_l");
    if (!rotation_root.has_value() || !rotation_arm.has_value()) {
        std::cerr << "Viewport rotation smoke could not find root/arm_l.\n";
        return false;
    }
    const auto rotation_parent =
        rotation_skeleton.data()->bones()[*rotation_arm].parent_index;
    if (!rotation_parent.has_value()) {
        std::cerr << "Viewport rotation smoke expected arm_l to have a parent.\n";
        return false;
    }
    const auto angle_matches = [](std::optional<double> actual, double expected) {
        return actual.has_value() && std::isfinite(*actual) &&
            std::abs(*actual - expected) <= 1e-4;
    };
    const auto pointer_from_matrix = [](
                                         const ViewportWorldPoint& pivot,
                                         double a,
                                         double b,
                                         double c,
                                         double d,
                                         double degrees) {
        const double radians = degrees * static_cast<double>(kPi) / 180.0;
        const double local_x = std::cos(radians);
        const double local_y = std::sin(radians);
        return ViewportWorldPoint{
            pivot.x + (a * local_x) + (b * local_y),
            pivot.y + (c * local_x) + (d * local_y)};
    };

    rotation_skeleton.set_scale(-2.0, 3.0);
    rotation_skeleton.update_world_transforms();
    const auto reflected_root_basis = viewport_interaction::rotation_basis(
        rotation_skeleton, *rotation_root);
    if (!reflected_root_basis.has_value() ||
        !angle_matches(
            viewport_interaction::rotation_angle(
                *reflected_root_basis,
                pointer_from_matrix(
                    reflected_root_basis->pivot_world,
                    -2.0,
                    0.0,
                    0.0,
                    3.0,
                    135.0)),
            135.0)) {
        std::cerr << "Viewport rotation root reflection basis was incorrect.\n";
        return false;
    }

    rotation_skeleton.set_scale(1.0, 1.0);
    auto& rotation_parent_pose =
        rotation_skeleton.bone_poses()[*rotation_parent].local_pose;
    rotation_parent_pose.x += 13.0f;
    rotation_parent_pose.y -= 17.0f;
    rotation_parent_pose.rotation = 37.0f;
    rotation_parent_pose.scale_x = 1.75f;
    rotation_parent_pose.scale_y = 0.55f;
    rotation_parent_pose.shear_x = 11.0f;
    rotation_parent_pose.shear_y = -9.0f;
    rotation_skeleton.bone_poses()[*rotation_arm].inherit =
        marrow::runtime::BoneInherit::Normal;
    rotation_skeleton.update_world_transforms();
    const auto normal_basis = viewport_interaction::rotation_basis(
        rotation_skeleton, *rotation_arm);
    const auto normal_parent_world =
        rotation_skeleton.bone_world_transforms()[*rotation_parent];
    if (!normal_basis.has_value() ||
        !angle_matches(
            viewport_interaction::rotation_angle(
                *normal_basis,
                pointer_from_matrix(
                    normal_basis->pivot_world,
                    normal_parent_world.a,
                    normal_parent_world.b,
                    normal_parent_world.c,
                    normal_parent_world.d,
                    -123.0)),
            -123.0)) {
        std::cerr << "Viewport rotation did not invert a translated/sheared parent.\n";
        return false;
    }

    rotation_parent_pose.scale_y = -0.55f;
    rotation_skeleton.update_world_transforms();
    const auto negative_determinant_basis = viewport_interaction::rotation_basis(
        rotation_skeleton, *rotation_arm);
    const auto reflected_parent_world =
        rotation_skeleton.bone_world_transforms()[*rotation_parent];
    if (!negative_determinant_basis.has_value() ||
        !angle_matches(
            viewport_interaction::rotation_angle(
                *negative_determinant_basis,
                pointer_from_matrix(
                    negative_determinant_basis->pivot_world,
                    reflected_parent_world.a,
                    reflected_parent_world.b,
                    reflected_parent_world.c,
                    reflected_parent_world.d,
                    72.0)),
            72.0)) {
        std::cerr << "Viewport rotation rejected a finite negative determinant.\n";
        return false;
    }

    rotation_skeleton.set_scale(-1.5, 2.25);
    rotation_skeleton.bone_poses()[*rotation_arm].inherit =
        marrow::runtime::BoneInherit::OnlyTranslation;
    rotation_skeleton.update_world_transforms();
    const auto translation_only_basis = viewport_interaction::rotation_basis(
        rotation_skeleton, *rotation_arm);
    if (!translation_only_basis.has_value() ||
        translation_only_basis->inherit != marrow::runtime::BoneInherit::OnlyTranslation ||
        !angle_matches(
            viewport_interaction::rotation_angle(
                *translation_only_basis,
                pointer_from_matrix(
                    translation_only_basis->pivot_world,
                    -1.5,
                    0.0,
                    0.0,
                    2.25,
                    -48.0)),
            -48.0)) {
        std::cerr << "Viewport rotation OnlyTranslation basis ignored skeleton scale.\n";
        return false;
    }

    for (const auto inherit : {
             marrow::runtime::BoneInherit::NoRotationOrReflection,
             marrow::runtime::BoneInherit::NoScale,
             marrow::runtime::BoneInherit::NoScaleOrReflection}) {
        rotation_skeleton.bone_poses()[*rotation_arm].inherit = inherit;
        rotation_skeleton.update_world_transforms();
        if (viewport_interaction::rotation_basis(
                rotation_skeleton, *rotation_arm).has_value()) {
            std::cerr << "Viewport rotation exposed an unsupported inherit mode.\n";
            return false;
        }
    }

    rotation_skeleton.bone_poses()[*rotation_arm].inherit =
        marrow::runtime::BoneInherit::Normal;
    rotation_parent_pose.scale_x = 0.0f;
    rotation_parent_pose.scale_y = 0.0f;
    rotation_skeleton.update_world_transforms();
    if (viewport_interaction::rotation_basis(
            rotation_skeleton, *rotation_arm).has_value()) {
        std::cerr << "Viewport rotation did not reject a singular parent basis.\n";
        return false;
    }
    rotation_parent_pose.scale_x = std::numeric_limits<float>::quiet_NaN();
    rotation_parent_pose.scale_y = 1.0f;
    rotation_skeleton.update_world_transforms();
    if (viewport_interaction::rotation_basis(
            rotation_skeleton, *rotation_arm).has_value()) {
        std::cerr << "Viewport rotation did not reject non-finite parent math.\n";
        return false;
    }

    if (std::abs(viewport_interaction::unwrap_rotation_delta(-340.0) - 20.0) > 1e-9 ||
        std::abs(viewport_interaction::unwrap_rotation_delta(340.0) + 20.0) > 1e-9 ||
        std::abs(viewport_interaction::unwrap_rotation_delta(-180.0) - 180.0) > 1e-9 ||
        std::abs(viewport_interaction::unwrap_rotation_delta(180.0) - 180.0) > 1e-9) {
        std::cerr << "Viewport rotation unwrap/tie contract failed.\n";
        return false;
    }
    double negative_multi_turn = 0.0;
    double previous_negative_angle = 0.0;
    for (const double angle : {-90.0, -180.0, 90.0, 0.0, -90.0}) {
        negative_multi_turn += viewport_interaction::unwrap_rotation_delta(
            angle - previous_negative_angle);
        previous_negative_angle = angle;
    }
    if (std::abs(negative_multi_turn + 450.0) > 1e-9 ||
        viewport_interaction::rotation_angle(
            *reflected_root_basis,
            ViewportWorldPoint{
                std::numeric_limits<double>::infinity(),
                reflected_root_basis->pivot_world.y})
            .has_value()) {
        std::cerr << "Viewport rotation negative multi-turn/Inf rejection failed.\n";
        return false;
    }

    const std::size_t root_index = *rotation_root;
    const auto& runtime_slots = camera_state.session.runtime_data()->slots();
    if (runtime_slots.empty() ||
        !scrub_timeline_time(
            &camera_state, 0.37, "Viewport rotation smoke", false)) {
        std::cerr << "Viewport rotation smoke could not stage its timeline context.\n";
        return false;
    }
    camera_state.selection.replace(
        marrow::editor::SlotSelection{runtime_slots.front().name});
    camera_state.selection.toggle(marrow::editor::BoneSelection{"root"});
    const marrow::editor::SelectionSet mixed_selection = camera_state.selection;
    camera_state.hierarchy_selection_anchor =
        marrow::editor::SlotSelection{runtime_slots.front().name};
    camera_state.selected_timeline_track_id = "viewport-rotate-focus";
    const auto mixed_anchor = camera_state.hierarchy_selection_anchor;
    const auto mixed_focus = camera_state.selected_timeline_track_id;
    const auto selection_matches = [](
                                       const marrow::editor::SelectionSet& left,
                                       const marrow::editor::SelectionSet& right) {
        if (left.items() != right.items()) return false;
        const auto* left_active = left.active();
        const auto* right_active = right.active();
        return (left_active == nullptr && right_active == nullptr) ||
            (left_active != nullptr && right_active != nullptr &&
             *left_active == *right_active);
    };
    auto rotation_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    if (!rotation_layout.has_value() || root_index >= rotation_layout->bones.size()) {
        std::cerr << "Viewport rotation smoke could not build a root layout.\n";
        return false;
    }
    ImVec2 rotation_center = rotation_layout->bones[root_index].screen_position;
    const auto ring_point = [](const ImVec2& center, double degrees) {
        const double radians = degrees * static_cast<double>(kPi) / 180.0;
        return ImVec2(
            center.x + static_cast<float>(58.0 * std::cos(radians)),
            center.y - static_cast<float>(58.0 * std::sin(radians)));
    };
    if (!viewport_interaction::rotation_gizmo_visible(
            camera_state, *rotation_layout) ||
        !viewport_interaction::hit_test_rotation_gizmo(
            camera_state, *rotation_layout, ring_point(rotation_center, 0.0)) ||
        !viewport_interaction::hit_test_rotation_gizmo(
            camera_state,
            *rotation_layout,
            ImVec2(rotation_center.x + 64.0f, rotation_center.y)) ||
        viewport_interaction::hit_test_rotation_gizmo(
            camera_state,
            *rotation_layout,
            ImVec2(rotation_center.x + 65.0f, rotation_center.y)) ||
        !viewport_interaction::hit_test_translate_gizmo(
            camera_state,
            *rotation_layout,
            ImVec2(rotation_center.x + 42.0f, rotation_center.y)).has_value()) {
        std::cerr << "Viewport rotation ring radius/band or translate priority target failed.\n";
        return false;
    }
    const double zoom_before_ring_test = camera_state.viewport.zoom;
    camera_state.viewport.zoom *= 1.8;
    const auto zoomed_rotation_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    if (!zoomed_rotation_layout.has_value() ||
        !viewport_interaction::hit_test_rotation_gizmo(
            camera_state,
            *zoomed_rotation_layout,
            ring_point(
                zoomed_rotation_layout->bones[root_index].screen_position,
                90.0))) {
        std::cerr << "Viewport rotation ring did not remain fixed in screen space.\n";
        return false;
    }
    camera_state.viewport.zoom = zoom_before_ring_test;
    rotation_layout = build_viewport_layout(camera_state, canvas_origin, canvas_size);
    rotation_center = rotation_layout->bones[root_index].screen_position;

    camera_state.preview_skeleton->bone_poses()[root_index].inherit =
        marrow::runtime::BoneInherit::NoScale;
    if (viewport_interaction::rotation_gizmo_visible(camera_state, *rotation_layout)) {
        std::cerr << "Viewport rotation ring remained visible for unsupported inherit.\n";
        return false;
    }
    camera_state.preview_skeleton->bone_poses()[root_index].inherit =
        marrow::runtime::BoneInherit::Normal;
    camera_state.selection.replace(
        marrow::editor::SlotSelection{runtime_slots.front().name});
    if (viewport_interaction::rotation_gizmo_visible(camera_state, *rotation_layout)) {
        std::cerr << "Viewport rotation ring remained visible for an active non-bone.\n";
        return false;
    }
    camera_state.selection = mixed_selection;
    camera_state.weight_paint.enabled = true;
    if (viewport_interaction::rotation_gizmo_visible(camera_state, *rotation_layout)) {
        std::cerr << "Viewport rotation ring remained visible during weight paint.\n";
        return false;
    }
    camera_state.weight_paint.enabled = false;

    const std::string click_project_before =
        marrow::editor::serialize_project(*camera_state.session.project());
    const std::size_t click_undo_before = camera_state.session.undo_count();
    const bool click_dirty_before = camera_state.session.dirty();
    camera_state.session.set_playing(true);
    sync_shell_from_editor_session(&camera_state);
    if (!viewport_interaction::begin_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 0.0)) ||
        camera_state.timeline_playing || !camera_state.session.transaction_active()) {
        std::cerr << "Viewport rotation did not pause playback and begin a transaction.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&camera_state, true);
    if (marrow::editor::serialize_project(*camera_state.session.project()) !=
            click_project_before ||
        camera_state.session.undo_count() != click_undo_before ||
        camera_state.session.dirty() != click_dirty_before ||
        camera_state.session.transaction_active() || camera_state.timeline_playing ||
        !selection_matches(camera_state.selection, mixed_selection) ||
        camera_state.hierarchy_selection_anchor != mixed_anchor ||
        camera_state.selected_timeline_track_id != mixed_focus) {
        std::cerr << "Viewport rotation click materialized a track or changed context.\n";
        return false;
    }

    const std::string rotation_project_before =
        marrow::editor::serialize_project(*camera_state.session.project());
    if (!viewport_interaction::begin_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 0.0))) {
        std::cerr << "Viewport multi-turn rotation gesture did not begin.\n";
        return false;
    }
    const auto* rotate_started = std::get_if<ViewportRotateGesturePayload>(
        &camera_state.viewport_transform_gesture->payload);
    if (rotate_started == nullptr ||
        std::abs(rotate_started->start_absolute_rotation) > 1e-6) {
        std::cerr << "Viewport rotation did not capture the raw effective start.\n";
        return false;
    }
    const ViewportRotationBasis frozen_basis = rotate_started->basis;
    for (const double degrees : {90.0, 180.0, 270.0, 360.0, 450.0}) {
        if (!viewport_interaction::update_rotate_gesture(
                &camera_state,
                *rotation_layout,
                ring_point(rotation_center, degrees))) {
            std::cerr << "Viewport multi-turn rotation update failed.\n";
            return false;
        }
    }
    const auto* rotate_live = std::get_if<ViewportRotateGesturePayload>(
        &camera_state.viewport_transform_gesture->payload);
    const auto* live_idle = camera_state.session.runtime_data()->find_animation("idle");
    const auto live_rotation = live_idle != nullptr
        ? live_idle->sample_bone_rotation(root_index, 0.37)
        : std::nullopt;
    if (rotate_live == nullptr ||
        std::abs(rotate_live->current_absolute_rotation - 450.0) > 1e-4 ||
        std::abs(rotate_live->basis.inverse_a - frozen_basis.inverse_a) > 1e-12 ||
        std::abs(rotate_live->basis.inverse_b - frozen_basis.inverse_b) > 1e-12 ||
        std::abs(rotate_live->basis.inverse_c - frozen_basis.inverse_c) > 1e-12 ||
        std::abs(rotate_live->basis.inverse_d - frozen_basis.inverse_d) > 1e-12 ||
        !live_rotation.has_value() || std::abs(*live_rotation - 450.0) > 1e-3) {
        std::cerr << "Viewport multi-turn accumulation or frozen basis drifted.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&camera_state, true);
    const std::string rotation_project_after =
        marrow::editor::serialize_project(*camera_state.session.project());
    const auto* root_rotate_edit =
        camera_state.session.project()->find_transform_timeline_edit(
            "idle", "root", marrow::editor::TransformTimelineChannel::Rotate);
    const bool has_raw_450_key = root_rotate_edit != nullptr && std::any_of(
        root_rotate_edit->keyframes.begin(),
        root_rotate_edit->keyframes.end(),
        [](const auto& key) {
            return std::abs(key.time - 0.37) <= 1e-6 &&
                std::abs(key.angle - 450.0) <= 1e-4;
        });
    if (!has_raw_450_key || rotation_project_after == rotation_project_before ||
        camera_state.session.undo_count() != click_undo_before + 1U ||
        !selection_matches(camera_state.selection, mixed_selection) ||
        camera_state.hierarchy_selection_anchor != mixed_anchor ||
        camera_state.selected_timeline_track_id != mixed_focus) {
        std::cerr << "Viewport rotation did not commit one raw multi-turn undo item.\n";
        return false;
    }

    if (!viewport_interaction::begin_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 0.0))) {
        return false;
    }
    const auto* raw_restart = std::get_if<ViewportRotateGesturePayload>(
        &camera_state.viewport_transform_gesture->payload);
    if (raw_restart == nullptr ||
        std::abs(raw_restart->start_absolute_rotation - 450.0) > 1e-3) {
        std::cerr << "Viewport rotation restarted from a normalized preview angle.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&camera_state, false);
    if (!camera_state.session.undo() ||
        marrow::editor::serialize_project(*camera_state.session.project()) !=
            rotation_project_before ||
        !camera_state.session.redo() ||
        marrow::editor::serialize_project(*camera_state.session.project()) !=
            rotation_project_after ||
        !camera_state.session.undo()) {
        std::cerr << "Viewport rotation undo/redo was not atomic.\n";
        return false;
    }
    sync_shell_from_editor_session(&camera_state);
    camera_state.session.clear_history();
    if (marrow::editor::serialize_project(*camera_state.session.project()) !=
        rotation_project_before) {
        return false;
    }

    const auto camera_preview_signature = [](const marrow::runtime::Skeleton& skeleton) {
        std::ostringstream stream;
        stream << std::setprecision(std::numeric_limits<double>::max_digits10);
        for (const auto& bone : skeleton.bone_poses()) {
            stream << bone.local_pose.x << ',' << bone.local_pose.y << ','
                   << bone.local_pose.rotation << ',' << bone.local_pose.scale_x << ','
                   << bone.local_pose.scale_y << ',' << bone.local_pose.shear_x << ','
                   << bone.local_pose.shear_y << ',' << static_cast<int>(bone.inherit) << ';';
        }
        for (const auto& world : skeleton.bone_world_transforms()) {
            stream << world.a << ',' << world.b << ',' << world.c << ',' << world.d << ','
                   << world.world_x << ',' << world.world_y << ';';
        }
        return stream.str();
    };
    const auto rollback_runtime_before = camera_state.session.runtime_data();
    const auto rollback_is_exact = [&]() {
        return marrow::editor::serialize_project(*camera_state.session.project()) ==
                rotation_project_before &&
            camera_state.session.runtime_data() == rollback_runtime_before &&
            camera_preview_signature(*camera_state.preview_skeleton) ==
                camera_preview_signature(
                    *marrow::editor::EditorSessionShellBinding::preview_skeleton(
                        camera_state.session)) &&
            camera_state.session.undo_count() == 0U &&
            camera_state.session.redo_count() == 0U &&
            !camera_state.session.dirty() && !camera_state.project_dirty &&
            !camera_state.session.transaction_active() &&
            !camera_state.viewport_transform_gesture.has_value() &&
            selection_matches(camera_state.selection, mixed_selection) &&
            camera_state.hierarchy_selection_anchor == mixed_anchor &&
            camera_state.selected_timeline_track_id == mixed_focus;
    };
    const std::string rollback_preview_before =
        camera_preview_signature(*camera_state.preview_skeleton);
    if (!viewport_interaction::begin_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 0.0)) ||
        !viewport_interaction::update_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 90.0))) {
        std::cerr << "Viewport pivot-rebase rollback gesture did not begin.\n";
        return false;
    }
    auto* rebase_rotate = std::get_if<ViewportRotateGesturePayload>(
        &camera_state.viewport_transform_gesture->payload);
    if (rebase_rotate == nullptr ||
        std::abs(rebase_rotate->accumulated_rotation - 90.0) > 1e-4 ||
        !viewport_interaction::update_rotate_gesture(
            &camera_state, *rotation_layout, rotation_center)) {
        std::cerr << "Viewport rotation did not suspend at its pivot.\n";
        return false;
    }
    rebase_rotate = std::get_if<ViewportRotateGesturePayload>(
        &camera_state.viewport_transform_gesture->payload);
    if (rebase_rotate == nullptr || !rebase_rotate->angular_reference_suspended ||
        !viewport_interaction::update_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 180.0))) {
        std::cerr << "Viewport rotation did not rebase after crossing its pivot.\n";
        return false;
    }
    rebase_rotate = std::get_if<ViewportRotateGesturePayload>(
        &camera_state.viewport_transform_gesture->payload);
    if (rebase_rotate == nullptr ||
        std::abs(rebase_rotate->accumulated_rotation - 90.0) > 1e-4 ||
        !viewport_interaction::update_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 270.0))) {
        std::cerr << "Viewport rotation pivot re-entry introduced a jump.\n";
        return false;
    }
    rebase_rotate = std::get_if<ViewportRotateGesturePayload>(
        &camera_state.viewport_transform_gesture->payload);
    if (rebase_rotate == nullptr ||
        std::abs(rebase_rotate->accumulated_rotation - 180.0) > 1e-4) {
        std::cerr << "Viewport rotation did not resume from its rebased angle.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&camera_state, false);
    if (!rollback_is_exact() ||
        camera_preview_signature(*camera_state.preview_skeleton) !=
            rollback_preview_before) {
        std::cerr << "Viewport rotation Escape rollback was not exact.\n";
        return false;
    }

    if (!viewport_interaction::begin_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 0.0)) ||
        !viewport_interaction::update_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ring_point(rotation_center, 90.0)) ||
        viewport_interaction::update_rotate_gesture(
            &camera_state,
            *rotation_layout,
            ImVec2(std::numeric_limits<float>::quiet_NaN(), rotation_center.y)) ||
        !rollback_is_exact() ||
        camera_preview_signature(*camera_state.preview_skeleton) !=
            rollback_preview_before) {
        std::cerr << "Viewport rotation non-finite failure rollback was not exact.\n";
        return false;
    }

    const auto transform_source_index =
        camera_state.session.runtime_data()->find_bone_index("transform_source");
    if (!transform_source_index.has_value() ||
        !scrub_timeline_time(
            &camera_state, 0.41, "Viewport setup rotation smoke", false)) {
        return false;
    }
    select_bone(&camera_state, *transform_source_index, "Smoke", false);
    auto setup_rotation_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    if (!setup_rotation_layout.has_value()) return false;
    const ImVec2 setup_rotation_center =
        setup_rotation_layout->bones[*transform_source_index].screen_position;
    const std::string setup_rotation_project_before =
        marrow::editor::serialize_project(*camera_state.session.project());
    if (!viewport_interaction::begin_rotate_gesture(
            &camera_state,
            *setup_rotation_layout,
            ring_point(setup_rotation_center, 0.0)) ||
        !viewport_interaction::update_rotate_gesture(
            &camera_state,
            *setup_rotation_layout,
            ring_point(setup_rotation_center, 90.0))) {
        std::cerr << "Viewport nonzero-setup rotation gesture failed.\n";
        return false;
    }
    const auto* setup_rotate = std::get_if<ViewportRotateGesturePayload>(
        &camera_state.viewport_transform_gesture->payload);
    const auto* setup_edit = camera_state.session.project()->find_transform_timeline_edit(
        "idle",
        "transform_source",
        marrow::editor::TransformTimelineChannel::Rotate);
    const bool setup_key_once = setup_edit != nullptr && std::any_of(
        setup_edit->keyframes.begin(), setup_edit->keyframes.end(), [](const auto& key) {
            return std::abs(key.time - 0.41) <= 1e-6 &&
                std::abs(key.angle - 90.0) <= 1e-4;
        });
    if (setup_rotate == nullptr ||
        std::abs(setup_rotate->start_absolute_rotation - 30.0) > 1e-4 ||
        std::abs(setup_rotate->current_absolute_rotation - 120.0) > 1e-4 ||
        !setup_key_once) {
        std::cerr << "Viewport rotation subtracted setup rotation more than once.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&camera_state, false);
    if (marrow::editor::serialize_project(*camera_state.session.project()) !=
            setup_rotation_project_before ||
        camera_state.session.undo_count() != 0U) {
        std::cerr << "Viewport nonzero-setup rollback was not exact.\n";
        return false;
    }

    if (!scrub_timeline_time(
            &camera_state, 0.37, "Viewport materialization smoke", false)) {
        return false;
    }
    const auto spine_index = camera_state.session.runtime_data()->find_bone_index("spine");
    const auto* idle_before_materialization =
        camera_state.session.runtime_data()->find_animation("idle");
    const auto* source_rotate =
        spine_index.has_value() && idle_before_materialization != nullptr
        ? idle_before_materialization->find_rotate_timeline(*spine_index)
        : nullptr;
    if (!spine_index.has_value() || source_rotate == nullptr) {
        return false;
    }
    const auto source_keys = source_rotate->keyframes;
    select_bone(&camera_state, *spine_index, "Smoke", false);
    auto materialization_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    if (!materialization_layout.has_value()) return false;
    const ImVec2 materialization_center =
        materialization_layout->bones[*spine_index].screen_position;
    const std::string materialization_project_before =
        marrow::editor::serialize_project(*camera_state.session.project());
    if (!viewport_interaction::begin_rotate_gesture(
            &camera_state,
            *materialization_layout,
            ring_point(materialization_center, 0.0)) ||
        !viewport_interaction::update_rotate_gesture(
            &camera_state,
            *materialization_layout,
            ring_point(materialization_center, 90.0))) {
        std::cerr << "Viewport imported rotation materialization failed.\n";
        return false;
    }
    const auto* materialized = camera_state.session.project()->find_transform_timeline_edit(
        "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
    const auto same_curve = [](const auto& left, const auto& right) {
        if (left.kind() != right.kind()) return false;
        const auto& l = left.cubic_bezier();
        const auto& r = right.cubic_bezier();
        return l.cx1 == r.cx1 && l.cy1 == r.cy1 &&
            l.cx2 == r.cx2 && l.cy2 == r.cy2;
    };
    bool source_keys_preserved = materialized != nullptr;
    for (const auto& source_key : source_keys) {
        const auto found = materialized == nullptr
            ? std::vector<marrow::editor::TransformKeyframeEdit>::const_iterator{}
            : std::find_if(
                  materialized->keyframes.begin(),
                  materialized->keyframes.end(),
                  [&](const auto& key) {
                      return std::abs(key.time - source_key.time) <= 1e-6;
                  });
        if (materialized == nullptr || found == materialized->keyframes.end() ||
            std::abs(found->angle - source_key.angle) > 1e-5 ||
            !same_curve(found->interpolation, source_key.interpolation)) {
            source_keys_preserved = false;
            break;
        }
    }
    if (!source_keys_preserved) {
        std::cerr << "Viewport rotation did not preserve imported keys/curves.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&camera_state, false);
    if (marrow::editor::serialize_project(*camera_state.session.project()) !=
            materialization_project_before ||
        camera_state.session.undo_count() != 0U) {
        std::cerr << "Viewport imported materialization rollback was not exact.\n";
        return false;
    }

    marrow::runtime::Skeleton scale_math_skeleton(
        camera_state.load_result.skeleton_data);
    const auto scale_root =
        scale_math_skeleton.data()->find_bone_index("root");
    const auto scale_arm =
        scale_math_skeleton.data()->find_bone_index("arm_l");
    if (!scale_root.has_value() || !scale_arm.has_value()) {
        std::cerr << "Viewport scale smoke could not find root/arm_l.\n";
        return false;
    }
    const auto scale_parent =
        scale_math_skeleton.data()->bones()[*scale_arm].parent_index;
    if (!scale_parent.has_value()) {
        std::cerr << "Viewport scale smoke expected arm_l to have a parent.\n";
        return false;
    }
    scale_math_skeleton.set_scale(-2.0, 3.0);
    scale_math_skeleton.update_world_transforms();
    const auto reflected_scale_basis = viewport_interaction::scale_basis(
        scale_math_skeleton, *scale_root);
    if (!reflected_scale_basis.has_value() ||
        reflected_scale_basis->positive_x_screen_direction.x > -0.99f ||
        reflected_scale_basis->positive_y_screen_direction.y > -0.99f) {
        std::cerr << "Viewport scale root reflection basis was incorrect.\n";
        return false;
    }

    scale_math_skeleton.set_scale(1.0, 1.0);
    auto& scale_parent_pose =
        scale_math_skeleton.bone_poses()[*scale_parent].local_pose;
    auto& scale_arm_pose =
        scale_math_skeleton.bone_poses()[*scale_arm];
    const auto expected_scale_screen_axis = [](
                                                double parent_a,
                                                double parent_b,
                                                double parent_c,
                                                double parent_d,
                                                double angle_degrees) {
        const double radians =
            angle_degrees * static_cast<double>(kPi) / 180.0;
        const double local_x = std::cos(radians);
        const double local_y = std::sin(radians);
        double screen_x = parent_a * local_x + parent_b * local_y;
        double screen_y = -(parent_c * local_x + parent_d * local_y);
        const double length = std::hypot(screen_x, screen_y);
        screen_x /= length;
        screen_y /= length;
        return ImVec2(
            static_cast<float>(screen_x),
            static_cast<float>(screen_y));
    };
    const auto scale_axis_matches = [](const ImVec2& actual, const ImVec2& expected) {
        return std::abs(
                   static_cast<double>(actual.x) -
                   static_cast<double>(expected.x)) <= 1e-5 &&
            std::abs(
                static_cast<double>(actual.y) -
                static_cast<double>(expected.y)) <= 1e-5;
    };
    scale_parent_pose.rotation = 37.0f;
    scale_parent_pose.scale_x = 1.75f;
    scale_parent_pose.scale_y = -0.55f;
    scale_parent_pose.shear_x = 11.0f;
    scale_parent_pose.shear_y = -9.0f;
    scale_arm_pose.inherit = marrow::runtime::BoneInherit::Normal;
    scale_arm_pose.local_pose.rotation = 23.0f;
    scale_arm_pose.local_pose.shear_x = 7.0f;
    scale_arm_pose.local_pose.shear_y = -13.0f;
    scale_arm_pose.local_pose.scale_x = -4.0f;
    scale_arm_pose.local_pose.scale_y = 0.25f;
    scale_math_skeleton.update_world_transforms();
    const auto normal_scale_basis = viewport_interaction::scale_basis(
        scale_math_skeleton, *scale_arm);
    if (!normal_scale_basis.has_value()) {
        std::cerr << "Viewport scale rejected a reflected non-uniform parent.\n";
        return false;
    }
    const double normal_scale_axis_determinant =
        static_cast<double>(
            normal_scale_basis->positive_x_screen_direction.x) *
            static_cast<double>(
                normal_scale_basis->positive_y_screen_direction.y) -
        static_cast<double>(
            normal_scale_basis->positive_x_screen_direction.y) *
            static_cast<double>(
                normal_scale_basis->positive_y_screen_direction.x);
    if (!std::isfinite(normal_scale_axis_determinant) ||
        std::abs(normal_scale_axis_determinant) <= 1e-4) {
        std::cerr << "Viewport scale produced a degenerate local-axis basis.\n";
        return false;
    }
    const auto scale_normal_parent_world =
        scale_math_skeleton.bone_world_transforms()[*scale_parent];
    const ImVec2 expected_normal_x = expected_scale_screen_axis(
        scale_normal_parent_world.a,
        scale_normal_parent_world.b,
        scale_normal_parent_world.c,
        scale_normal_parent_world.d,
        static_cast<double>(scale_arm_pose.local_pose.rotation) +
            static_cast<double>(scale_arm_pose.local_pose.shear_x));
    const ImVec2 expected_normal_y = expected_scale_screen_axis(
        scale_normal_parent_world.a,
        scale_normal_parent_world.b,
        scale_normal_parent_world.c,
        scale_normal_parent_world.d,
        static_cast<double>(scale_arm_pose.local_pose.rotation) + 90.0 +
            static_cast<double>(scale_arm_pose.local_pose.shear_y));
    if (!scale_axis_matches(
            normal_scale_basis->positive_x_screen_direction,
            expected_normal_x) ||
        !scale_axis_matches(
            normal_scale_basis->positive_y_screen_direction,
            expected_normal_y)) {
        std::cerr << "Viewport scale Normal axes ignored evaluated parent rotation/shear.\n";
        return false;
    }

    scale_math_skeleton.set_scale(-1.5, 2.25);
    scale_arm_pose.inherit =
        marrow::runtime::BoneInherit::OnlyTranslation;
    scale_math_skeleton.update_world_transforms();
    const auto translation_only_scale_basis =
        viewport_interaction::scale_basis(
            scale_math_skeleton, *scale_arm);
    if (!translation_only_scale_basis.has_value() ||
        translation_only_scale_basis->inherit !=
            marrow::runtime::BoneInherit::OnlyTranslation) {
        std::cerr << "Viewport scale OnlyTranslation basis ignored skeleton scale.\n";
        return false;
    }
    const ImVec2 expected_translation_only_x = expected_scale_screen_axis(
        -1.5,
        0.0,
        0.0,
        2.25,
        static_cast<double>(scale_arm_pose.local_pose.rotation) +
            static_cast<double>(scale_arm_pose.local_pose.shear_x));
    const ImVec2 expected_translation_only_y = expected_scale_screen_axis(
        -1.5,
        0.0,
        0.0,
        2.25,
        static_cast<double>(scale_arm_pose.local_pose.rotation) + 90.0 +
            static_cast<double>(scale_arm_pose.local_pose.shear_y));
    if (!scale_axis_matches(
            translation_only_scale_basis->positive_x_screen_direction,
            expected_translation_only_x) ||
        !scale_axis_matches(
            translation_only_scale_basis->positive_y_screen_direction,
            expected_translation_only_y)) {
        std::cerr << "Viewport scale OnlyTranslation axes used the live parent basis.\n";
        return false;
    }

    for (const auto inherit : {
             marrow::runtime::BoneInherit::NoRotationOrReflection,
             marrow::runtime::BoneInherit::NoScale,
             marrow::runtime::BoneInherit::NoScaleOrReflection}) {
        scale_arm_pose.inherit = inherit;
        scale_math_skeleton.update_world_transforms();
        if (viewport_interaction::scale_basis(
                scale_math_skeleton, *scale_arm).has_value()) {
            std::cerr << "Viewport scale exposed an unsupported inherit mode.\n";
            return false;
        }
    }

    scale_math_skeleton.set_scale(1.0, 1.0);
    scale_arm_pose.inherit = marrow::runtime::BoneInherit::Normal;
    scale_parent_pose.scale_x = 0.0f;
    scale_parent_pose.scale_y = 0.0f;
    scale_math_skeleton.update_world_transforms();
    if (viewport_interaction::scale_basis(
            scale_math_skeleton, *scale_arm).has_value()) {
        std::cerr << "Viewport scale did not reject a singular parent basis.\n";
        return false;
    }
    scale_parent_pose.scale_x = 1.0f;
    scale_parent_pose.scale_y = 1.0f;
    scale_arm_pose.local_pose.shear_x = 0.0f;
    scale_arm_pose.local_pose.shear_y = -90.0f;
    scale_math_skeleton.update_world_transforms();
    if (viewport_interaction::scale_basis(
            scale_math_skeleton, *scale_arm).has_value()) {
        std::cerr << "Viewport scale did not reject degenerate local axes.\n";
        return false;
    }
    scale_arm_pose.local_pose.shear_y = 0.0f;
    scale_parent_pose.scale_x =
        std::numeric_limits<float>::quiet_NaN();
    scale_math_skeleton.update_world_transforms();
    if (viewport_interaction::scale_basis(
            scale_math_skeleton, *scale_arm).has_value()) {
        std::cerr << "Viewport scale did not reject non-finite parent math.\n";
        return false;
    }
    scale_parent_pose.scale_x =
        std::numeric_limits<float>::infinity();
    scale_math_skeleton.update_world_transforms();
    if (viewport_interaction::scale_basis(
            scale_math_skeleton, *scale_arm).has_value()) {
        std::cerr << "Viewport scale did not reject infinite parent math.\n";
        return false;
    }

    if (!scrub_timeline_time(
            &camera_state, 0.37, "Viewport scale smoke", false)) {
        return false;
    }
    const auto* scale_idle =
        camera_state.session.runtime_data()->find_animation("idle");
    const auto* source_scale =
        scale_idle != nullptr
        ? scale_idle->find_scale_timeline(*spine_index)
        : nullptr;
    const auto start_scale =
        scale_idle != nullptr
        ? scale_idle->sample_bone_scale(*spine_index, 0.37)
        : std::nullopt;
    if (source_scale == nullptr || !start_scale.has_value()) {
        std::cerr << "Viewport scale smoke requires an effective scale track.\n";
        return false;
    }
    const auto source_scale_keys = source_scale->keyframes;
    camera_state.selection.replace(
        marrow::editor::SlotSelection{runtime_slots.front().name});
    camera_state.selection.toggle(
        marrow::editor::BoneSelection{"spine"});
    const marrow::editor::SelectionSet scale_mixed_selection =
        camera_state.selection;
    camera_state.hierarchy_selection_anchor =
        marrow::editor::SlotSelection{runtime_slots.front().name};
    camera_state.selected_timeline_track_id =
        "viewport-scale-focus";
    const auto scale_anchor =
        camera_state.hierarchy_selection_anchor;
    const auto scale_focus =
        camera_state.selected_timeline_track_id;

    if (viewport_interaction::press_target(
            true, true, true, true, true, true, true) !=
            ViewportPressTarget::ActiveGesture ||
        viewport_interaction::press_target(
            false, true, true, true, true, true, true) !=
            ViewportPressTarget::WeightBrush ||
        viewport_interaction::press_target(
            false, false, true, true, true, true, true) !=
            ViewportPressTarget::Translate ||
        viewport_interaction::press_target(
            false, false, false, true, true, true, true) !=
            ViewportPressTarget::Rotation ||
        viewport_interaction::press_target(
            false, false, false, false, true, true, true) !=
            ViewportPressTarget::Scale ||
        viewport_interaction::press_target(
            false, false, false, false, false, true, true) !=
            ViewportPressTarget::FfdVertex ||
        viewport_interaction::press_target(
            false, false, false, false, false, false, true) !=
            ViewportPressTarget::Entity ||
        viewport_interaction::press_target(
            false, false, false, false, false, false, false) !=
            ViewportPressTarget::Box) {
        std::cerr << "Viewport scale press arbitration order changed.\n";
        return false;
    }

    auto scale_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    const auto scale_basis = viewport_interaction::scale_basis(
        *camera_state.preview_skeleton, *spine_index);
    if (!scale_layout.has_value() || !scale_basis.has_value() ||
        !viewport_interaction::scale_gizmo_visible(
            camera_state, *scale_layout)) {
        std::cerr << "Viewport scale gizmo was not visible for an active Bone.\n";
        return false;
    }
    const auto scale_mage_skin =
        camera_state.load_result.skeleton_data->find_skin_index("mage");
    const auto scale_arm_slot =
        camera_state.load_result.skeleton_data->find_slot_index("arm_l");
    const auto& scale_ik_constraints =
        camera_state.load_result.skeleton_data->ik_constraints();
    if (!scale_mage_skin.has_value() || !scale_arm_slot.has_value() ||
        scale_ik_constraints.empty() ||
        camera_state.load_result.skeleton_data->find_attachment(
            *scale_mage_skin, *scale_arm_slot, "mage_arm_l") == nullptr) {
        std::cerr << "Viewport scale non-Bone visibility smoke lacks fixture identities.\n";
        return false;
    }
    const std::array<marrow::editor::SelectionItem, 3> scale_non_bones{
        marrow::editor::SlotSelection{runtime_slots.front().name},
        marrow::editor::AttachmentSelection{
            "arm_l", "mage", "mage_arm_l"},
        marrow::editor::ConstraintSelection{
            ConstraintKind::Ik, scale_ik_constraints.front().name}};
    for (const auto& non_bone : scale_non_bones) {
        camera_state.selection.replace(non_bone);
        if (viewport_interaction::scale_gizmo_visible(
                camera_state, *scale_layout)) {
            std::cerr << "Viewport scale remained visible for an active non-Bone.\n";
            return false;
        }
    }
    camera_state.selection = scale_mixed_selection;
    camera_state.weight_paint.enabled = true;
    if (viewport_interaction::scale_gizmo_visible(
            camera_state, *scale_layout)) {
        std::cerr << "Viewport scale remained visible during weight paint.\n";
        return false;
    }
    camera_state.weight_paint.enabled = false;
    auto& scale_preview_pose =
        camera_state.preview_skeleton->bone_poses()[*spine_index];
    const auto scale_preview_inherit = scale_preview_pose.inherit;
    scale_preview_pose.inherit = marrow::runtime::BoneInherit::NoScale;
    const char* unsupported_scale_hint =
        viewport_interaction::transform_hint(
            camera_state, *scale_layout);
    if (viewport_interaction::scale_gizmo_visible(
            camera_state, *scale_layout) ||
        unsupported_scale_hint == nullptr ||
        std::string_view(unsupported_scale_hint).find("noScale") ==
            std::string_view::npos) {
        std::cerr << "Viewport scale unsupported-inherit hint was missing.\n";
        return false;
    }
    scale_preview_pose.inherit = scale_preview_inherit;

    const auto scale_screen_point = [](
                                        const ViewportLayout& layout,
                                        const ViewportScaleBasis& basis,
                                        const ImVec2& direction,
                                        double projection) {
        const ImVec2 pivot = screen_from_world(
            layout, basis.pivot_world.x, basis.pivot_world.y);
        return ImVec2(
            pivot.x + static_cast<float>(
                          static_cast<double>(direction.x) * projection),
            pivot.y + static_cast<float>(
                          static_cast<double>(direction.y) * projection));
    };
    const ImVec2 scale_pivot = scale_screen_point(
        *scale_layout,
        *scale_basis,
        scale_basis->positive_x_screen_direction,
        0.0);
    const ImVec2 scale_x_handle = scale_screen_point(
        *scale_layout,
        *scale_basis,
        scale_basis->positive_x_screen_direction,
        74.0);
    const ImVec2 scale_x_miss = scale_screen_point(
        *scale_layout,
        *scale_basis,
        scale_basis->positive_x_screen_direction,
        81.0);
    if (viewport_interaction::hit_test_scale_gizmo(
            camera_state, *scale_layout, scale_x_handle) !=
            std::optional<ViewportScaleHandle>(
                ViewportScaleHandle::X) ||
        viewport_interaction::hit_test_scale_gizmo(
            camera_state, *scale_layout, scale_x_miss).has_value()) {
        std::cerr << "Viewport scale 74px radius or 6px hit contract failed.\n";
        return false;
    }
    ViewportLayout zoomed_scale_layout = *scale_layout;
    zoomed_scale_layout.pixels_per_unit *= 2.0f;
    const ImVec2 zoomed_scale_x_handle = scale_screen_point(
        zoomed_scale_layout,
        *scale_basis,
        scale_basis->positive_x_screen_direction,
        74.0);
    if (viewport_interaction::hit_test_scale_gizmo(
            camera_state,
            zoomed_scale_layout,
            zoomed_scale_x_handle) !=
        std::optional<ViewportScaleHandle>(
            ViewportScaleHandle::X)) {
        std::cerr << "Viewport scale handles changed size with zoom.\n";
        return false;
    }

    ViewportScaleGesturePayload scale_math_payload;
    scale_math_payload.basis = *scale_basis;
    scale_math_payload.start_projection_pixels = 74.0;
    scale_math_payload.start_absolute_scale_x = 2.0;
    scale_math_payload.start_absolute_scale_y = -3.0;
    scale_math_payload.handle = ViewportScaleHandle::X;
    const auto pivot_zero = viewport_interaction::scale_candidate(
        scale_math_payload, *scale_layout, scale_pivot);
    const auto x_sign_cross = viewport_interaction::scale_candidate(
        scale_math_payload,
        *scale_layout,
        scale_screen_point(
            *scale_layout,
            *scale_basis,
            scale_basis->positive_x_screen_direction,
            -74.0));
    scale_math_payload.start_absolute_scale_x = 0.0;
    const auto zero_start_recovery =
        viewport_interaction::scale_candidate(
            scale_math_payload,
            *scale_layout,
            scale_screen_point(
                *scale_layout,
                *scale_basis,
                scale_basis->positive_x_screen_direction,
                148.0));
    scale_math_payload.handle = ViewportScaleHandle::Uniform;
    scale_math_payload.start_absolute_scale_x = -2.0;
    scale_math_payload.start_absolute_scale_y = 0.5;
    const auto uniform_flip = viewport_interaction::scale_candidate(
        scale_math_payload,
        *scale_layout,
        scale_screen_point(
            *scale_layout,
            *scale_basis,
            scale_basis->uniform_screen_direction,
            -74.0));
    scale_math_payload.start_absolute_scale_x = 0.0;
    scale_math_payload.start_absolute_scale_y = 0.0;
    const auto zero_uniform = viewport_interaction::scale_candidate(
        scale_math_payload,
        *scale_layout,
        scale_screen_point(
            *scale_layout,
            *scale_basis,
            scale_basis->uniform_screen_direction,
            148.0));
    constexpr double kScaleScreenTolerance = 1e-5;
    if (!pivot_zero.has_value() || pivot_zero->scale_x != 0.0 ||
        std::abs(pivot_zero->scale_y + 3.0) > kScaleScreenTolerance ||
        !x_sign_cross.has_value() ||
        std::abs(x_sign_cross->scale_x + 2.0) > kScaleScreenTolerance ||
        std::abs(x_sign_cross->scale_y + 3.0) > kScaleScreenTolerance ||
        !zero_start_recovery.has_value() ||
        std::abs(zero_start_recovery->scale_x - 1.0) >
            kScaleScreenTolerance ||
        std::abs(zero_start_recovery->scale_y + 3.0) >
            kScaleScreenTolerance ||
        !uniform_flip.has_value() ||
        std::abs(uniform_flip->scale_x - 2.0) > kScaleScreenTolerance ||
        std::abs(uniform_flip->scale_y + 0.5) > kScaleScreenTolerance ||
        zero_uniform.has_value()) {
        std::cerr << "Viewport signed scale mapping contract failed.\n";
        return false;
    }

    const std::string scale_project_before =
        marrow::editor::serialize_project(*camera_state.session.project());
    const std::size_t scale_undo_before =
        camera_state.session.undo_count();
    camera_state.timeline_playing = true;
    camera_state.session.set_playing(true);
    if (!viewport_interaction::begin_scale_gesture(
            &camera_state,
            *scale_layout,
            ViewportScaleHandle::X,
            scale_x_handle) ||
        camera_state.timeline_playing ||
        !camera_state.session.transaction_active()) {
        std::cerr << "Viewport scale did not pause playback and begin a transaction.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(
        &camera_state, true);
    if (marrow::editor::serialize_project(
            *camera_state.session.project()) != scale_project_before ||
        camera_state.session.undo_count() != scale_undo_before) {
        std::cerr << "Viewport scale click materialized a track or history.\n";
        return false;
    }

    if (!viewport_interaction::begin_scale_gesture(
            &camera_state,
            *scale_layout,
            ViewportScaleHandle::X,
            scale_x_handle) ||
        !viewport_interaction::update_scale_gesture(
            &camera_state, *scale_layout, scale_pivot)) {
        std::cerr << "Viewport scale gesture could not reach exact zero.\n";
        return false;
    }
    const auto* live_scale =
        std::get_if<ViewportScaleGesturePayload>(
            &camera_state.viewport_transform_gesture->payload);
    const auto* materialized_scale =
        camera_state.session.project()->find_transform_timeline_edit(
            "idle",
            "spine",
            marrow::editor::TransformTimelineChannel::Scale);
    const auto curve_matches = [](const auto& left, const auto& right) {
        if (left.kind() != right.kind()) return false;
        if (left.kind() !=
            marrow::runtime::InterpolationKind::CubicBezier) {
            return true;
        }
        const auto& l = left.cubic_bezier();
        const auto& r = right.cubic_bezier();
        return l.cx1 == r.cx1 && l.cy1 == r.cy1 &&
            l.cx2 == r.cx2 && l.cy2 == r.cy2;
    };
    bool scale_source_keys_preserved =
        materialized_scale != nullptr;
    for (const auto& source_key : source_scale_keys) {
        const auto found = materialized_scale == nullptr
            ? std::vector<
                  marrow::editor::TransformKeyframeEdit>::const_iterator{}
            : std::find_if(
                  materialized_scale->keyframes.begin(),
                  materialized_scale->keyframes.end(),
                  [&](const auto& key) {
                      return std::abs(
                                 key.time - source_key.time) <= 1e-6;
                  });
        if (materialized_scale == nullptr ||
            found == materialized_scale->keyframes.end() ||
            std::abs(found->x - source_key.x) > 1e-6 ||
            std::abs(found->y - source_key.y) > 1e-6 ||
            !curve_matches(
                found->interpolation,
                source_key.interpolation)) {
            scale_source_keys_preserved = false;
            break;
        }
    }
    const auto& live_preview_scale_pose =
        camera_state.preview_skeleton->bone_poses()[*spine_index].local_pose;
    if (live_scale == nullptr ||
        live_scale->current_absolute_scale_x != 0.0 ||
        std::abs(
            live_scale->current_absolute_scale_y -
            start_scale->y) > 1e-5 ||
        live_preview_scale_pose.scale_x != 0.0f ||
        std::abs(
            static_cast<double>(live_preview_scale_pose.scale_y) -
            start_scale->y) > 1e-5 ||
        !scale_source_keys_preserved) {
        std::cerr << "Viewport scale live preview or materialization failed.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(
        &camera_state, true);
    const std::string scale_project_zero =
        marrow::editor::serialize_project(*camera_state.session.project());
    if (scale_project_zero == scale_project_before ||
        camera_state.session.undo_count() !=
            scale_undo_before + 1U ||
        !selection_matches(
            camera_state.selection, scale_mixed_selection) ||
        camera_state.hierarchy_selection_anchor != scale_anchor ||
        camera_state.selected_timeline_track_id != scale_focus) {
        std::cerr << "Viewport scale did not commit one active-only undo item.\n";
        return false;
    }

    scale_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    const auto zero_scale_basis = viewport_interaction::scale_basis(
        *camera_state.preview_skeleton, *spine_index);
    if (!scale_layout.has_value() ||
        !zero_scale_basis.has_value()) {
        return false;
    }
    const ImVec2 zero_scale_x_handle = scale_screen_point(
        *scale_layout,
        *zero_scale_basis,
        zero_scale_basis->positive_x_screen_direction,
        74.0);
    const ImVec2 zero_scale_x_recovery = scale_screen_point(
        *scale_layout,
        *zero_scale_basis,
        zero_scale_basis->positive_x_screen_direction,
        148.0);
    const auto zero_scale_runtime_before =
        camera_state.session.runtime_data();
    const std::string zero_scale_preview_before =
        camera_preview_signature(*camera_state.preview_skeleton);
    const bool zero_scale_dirty_before =
        camera_state.session.dirty();
    const bool zero_scale_project_dirty_before =
        camera_state.project_dirty;
    const std::size_t zero_scale_undo_before =
        camera_state.session.undo_count();
    const std::size_t zero_scale_redo_before =
        camera_state.session.redo_count();
    const auto scale_rollback_is_exact = [&]() {
        return marrow::editor::serialize_project(
                   *camera_state.session.project()) ==
                scale_project_zero &&
            camera_state.session.runtime_data() ==
                zero_scale_runtime_before &&
            camera_preview_signature(
                *camera_state.preview_skeleton) ==
                zero_scale_preview_before &&
            camera_state.session.dirty() ==
                zero_scale_dirty_before &&
            camera_state.project_dirty ==
                zero_scale_project_dirty_before &&
            camera_state.session.undo_count() ==
                zero_scale_undo_before &&
            camera_state.session.redo_count() ==
                zero_scale_redo_before &&
            !camera_state.session.transaction_active() &&
            !camera_state.viewport_transform_gesture.has_value() &&
            selection_matches(
                camera_state.selection, scale_mixed_selection) &&
            camera_state.hierarchy_selection_anchor ==
                scale_anchor &&
            camera_state.selected_timeline_track_id ==
                scale_focus;
    };
    if (!viewport_interaction::begin_scale_gesture(
            &camera_state,
            *scale_layout,
            ViewportScaleHandle::X,
            zero_scale_x_handle) ||
        !viewport_interaction::update_scale_gesture(
            &camera_state,
            *scale_layout,
            zero_scale_x_recovery)) {
        std::cerr << "Viewport scale could not recover an exact-zero axis.\n";
        return false;
    }
    const auto* recovered_scale =
        std::get_if<ViewportScaleGesturePayload>(
            &camera_state.viewport_transform_gesture->payload);
    if (recovered_scale == nullptr ||
        std::abs(
            recovered_scale->current_absolute_scale_x - 1.0) >
            1e-5) {
        return false;
    }
    viewport_interaction::finish_transform_gesture(
        &camera_state, false);
    if (!scale_rollback_is_exact()) {
        std::cerr << "Viewport scale cancel rollback was not exact.\n";
        return false;
    }
    if (!viewport_interaction::begin_scale_gesture(
            &camera_state,
            *scale_layout,
            ViewportScaleHandle::X,
            zero_scale_x_handle) ||
        !viewport_interaction::update_scale_gesture(
            &camera_state,
            *scale_layout,
            zero_scale_x_recovery) ||
        marrow::editor::serialize_project(
            *camera_state.session.project()) == scale_project_zero ||
        camera_preview_signature(
            *camera_state.preview_skeleton) ==
            zero_scale_preview_before ||
        viewport_interaction::update_scale_gesture(
            &camera_state,
            *scale_layout,
            ImVec2(
                std::numeric_limits<float>::quiet_NaN(),
                zero_scale_x_handle.y)) ||
        !scale_rollback_is_exact()) {
        std::cerr << "Viewport scale non-finite rollback was not exact.\n";
        return false;
    }
    if (!viewport_interaction::begin_scale_gesture(
            &camera_state,
            *scale_layout,
            ViewportScaleHandle::X,
            zero_scale_x_handle) ||
        !viewport_interaction::update_scale_gesture(
            &camera_state,
            *scale_layout,
            zero_scale_x_recovery) ||
        viewport_interaction::update_scale_gesture(
            &camera_state,
            *scale_layout,
            ImVec2(
                std::numeric_limits<float>::infinity(),
                zero_scale_x_handle.y)) ||
        !scale_rollback_is_exact()) {
        std::cerr << "Viewport scale infinite rollback was not exact.\n";
        return false;
    }
    if (!camera_state.session.undo() ||
        marrow::editor::serialize_project(
            *camera_state.session.project()) != scale_project_before ||
        !camera_state.session.redo() ||
        marrow::editor::serialize_project(
            *camera_state.session.project()) != scale_project_zero ||
        !camera_state.session.undo()) {
        std::cerr << "Viewport scale undo/redo was not atomic.\n";
        return false;
    }
    sync_shell_from_editor_session(&camera_state);
    camera_state.session.clear_history();
    scale_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    const auto uniform_zero_basis = viewport_interaction::scale_basis(
        *camera_state.preview_skeleton, *spine_index);
    if (!scale_layout.has_value() ||
        !uniform_zero_basis.has_value()) {
        return false;
    }
    const ImVec2 uniform_zero_handle = scale_screen_point(
        *scale_layout,
        *uniform_zero_basis,
        uniform_zero_basis->uniform_screen_direction,
        74.0);
    const ImVec2 uniform_zero_pivot = scale_screen_point(
        *scale_layout,
        *uniform_zero_basis,
        uniform_zero_basis->uniform_screen_direction,
        0.0);
    if (!viewport_interaction::begin_scale_gesture(
            &camera_state,
            *scale_layout,
            ViewportScaleHandle::Uniform,
            uniform_zero_handle) ||
        !viewport_interaction::update_scale_gesture(
            &camera_state,
            *scale_layout,
            uniform_zero_pivot)) {
        std::cerr << "Viewport uniform scale could not reach (0,0).\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(
        &camera_state, true);
    scale_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    const char* zero_uniform_hint =
        scale_layout.has_value()
        ? viewport_interaction::transform_hint(
              camera_state, *scale_layout)
        : nullptr;
    const auto& zero_uniform_preview =
        camera_state.preview_skeleton->bone_poses()[*spine_index].local_pose;
    if (!scale_layout.has_value() ||
        viewport_interaction::uniform_scale_handle_visible(
            camera_state, *scale_layout) ||
        zero_uniform_hint == nullptr ||
        std::string_view(zero_uniform_hint).find("both axes are zero") ==
            std::string_view::npos ||
        zero_uniform_preview.scale_x != 0.0f ||
        zero_uniform_preview.scale_y != 0.0f ||
        camera_state.session.undo_count() != 1U) {
        std::cerr << "Viewport (0,0) uniform hide/hint contract failed.\n";
        return false;
    }
    if (!camera_state.session.undo()) {
        return false;
    }
    sync_shell_from_editor_session(&camera_state);
    camera_state.session.clear_history();
    if (marrow::editor::serialize_project(
            *camera_state.session.project()) != scale_project_before) {
        return false;
    }

    const auto exercise_translate_gesture = [&camera_state, canvas_origin, canvas_size](
                                                std::string_view bone_name,
                                                double time,
                                                const ImVec2& screen_delta) {
        const auto bone_index = camera_state.session.runtime_data()->find_bone_index(bone_name);
        if (!bone_index.has_value() ||
            !scrub_timeline_time(
                &camera_state, time, "Viewport gesture smoke", false)) {
            return false;
        }
        select_bone(&camera_state, *bone_index, "Smoke", false);
        const auto layout = build_viewport_layout(
            camera_state, canvas_origin, canvas_size);
        if (!layout.has_value() ||
            *bone_index >= camera_state.preview_skeleton->bone_world_transforms().size()) {
            return false;
        }
        const auto world =
            camera_state.preview_skeleton->bone_world_transforms()[*bone_index];
        const ImVec2 pointer = screen_from_world(
            *layout, world.world_x, world.world_y);
        const std::string before =
            marrow::editor::serialize_project(*camera_state.session.project());
        if (!viewport_interaction::begin_translate_gesture(
                &camera_state,
                *layout,
                ViewportTranslateAxis::Free,
                pointer) ||
            !viewport_interaction::update_translate_gesture(
                &camera_state,
                *layout,
                ImVec2(pointer.x + screen_delta.x, pointer.y + screen_delta.y))) {
            return false;
        }
        viewport_interaction::finish_transform_gesture(&camera_state, true);
        const auto* edit = camera_state.session.project()->find_transform_timeline_edit(
            "idle",
            bone_name,
            marrow::editor::TransformTimelineChannel::Translate);
        const bool keyed = edit != nullptr && std::any_of(
            edit->keyframes.begin(), edit->keyframes.end(), [&](const auto& key) {
                return std::abs(key.time - time) <= 1e-6;
            });
        const std::string after =
            marrow::editor::serialize_project(*camera_state.session.project());
        if (!keyed || before == after || camera_state.session.undo_count() != 1U ||
            camera_state.timeline_playing) {
            return false;
        }
        if (!camera_state.session.undo() ||
            marrow::editor::serialize_project(*camera_state.session.project()) != before ||
            !camera_state.session.redo() ||
            marrow::editor::serialize_project(*camera_state.session.project()) != after ||
            !camera_state.session.undo()) {
            return false;
        }
        sync_shell_from_editor_session(&camera_state);
        camera_state.session.clear_history();
        return marrow::editor::serialize_project(*camera_state.session.project()) == before;
    };
    if (!exercise_translate_gesture("root", 0.37, ImVec2(18.0f, -9.0f)) ||
        !exercise_translate_gesture("arm_l", 0.43, ImVec2(-14.0f, 12.0f)) ||
        !exercise_translate_gesture("ik_target", 0.47, ImVec2(11.0f, 16.0f))) {
        std::cerr << "Viewport translate gesture did not auto-key/undo/redo atomically.\n";
        return false;
    }

    const std::string singular_before =
        marrow::editor::serialize_project(*camera_state.session.project());
    if (!scrub_timeline_time(
            &camera_state, 0.51, "Viewport singular smoke", false)) {
        return false;
    }
    const auto singular_arm = camera_state.session.runtime_data()->find_bone_index("arm_l");
    if (!singular_arm.has_value()) return false;
    const auto singular_parent =
        camera_state.session.runtime_data()->bones()[*singular_arm].parent_index;
    if (!singular_parent.has_value()) return false;
    select_bone(&camera_state, *singular_arm, "Smoke", false);
    auto& singular_parent_pose =
        camera_state.preview_skeleton->bone_poses()[*singular_parent].local_pose;
    singular_parent_pose.scale_x = 0.0f;
    singular_parent_pose.scale_y = 0.0f;
    camera_state.preview_skeleton->update_world_transforms();
    const auto singular_layout = build_viewport_layout(
        camera_state, canvas_origin, canvas_size);
    if (!singular_layout.has_value()) return false;
    const auto singular_world =
        camera_state.preview_skeleton->bone_world_transforms()[*singular_arm];
    const ImVec2 singular_pointer = screen_from_world(
        *singular_layout, singular_world.world_x, singular_world.world_y);
    if (!viewport_interaction::begin_translate_gesture(
            &camera_state,
            *singular_layout,
            ViewportTranslateAxis::Free,
            singular_pointer) ||
        viewport_interaction::update_translate_gesture(
            &camera_state,
            *singular_layout,
            ImVec2(singular_pointer.x + 10.0f, singular_pointer.y + 10.0f)) ||
        camera_state.viewport_transform_gesture.has_value() ||
        marrow::editor::serialize_project(*camera_state.session.project()) != singular_before) {
        std::cerr << "Viewport singular-parent gesture did not cancel without mutation.\n";
        return false;
    }

    const ImVec2 zoom_anchor(811.25f, 233.75f);
    const ViewportWorldPoint anchor_before =
        world_from_screen(*initial_layout, zoom_anchor);
    if (!zoom_viewport_at_screen_position(
            &camera_state,
            canvas_origin,
            canvas_size,
            zoom_anchor,
            1.1)) {
        std::cerr << "Viewport camera smoke could not apply cursor-centered zoom.\n";
        return false;
    }
    const auto zoomed_layout = build_viewport_layout(
        camera_state,
        canvas_origin,
        canvas_size);
    if (!zoomed_layout.has_value()) {
        std::cerr << "Viewport camera smoke could not build its zoomed layout.\n";
        return false;
    }
    const ViewportWorldPoint anchor_after =
        world_from_screen(*zoomed_layout, zoom_anchor);
    if (std::abs(anchor_after.x - anchor_before.x) > 1e-4 ||
        std::abs(anchor_after.y - anchor_before.y) > 1e-4 ||
        std::abs(zoomed_layout->pixels_per_unit - initial_layout->pixels_per_unit) < 1e-3f) {
        std::cerr << "Viewport zoom did not preserve the world point under the cursor.\n";
        return false;
    }

    const ViewportCamera stable_camera = camera_state.viewport_camera;
    const float stable_pixels_per_unit = zoomed_layout->pixels_per_unit;
    const ImVec2 root_screen_before = zoomed_layout->bones.front().screen_position;
    camera_state.preview_skeleton->bone_poses().front().local_pose.x += 250.0f;
    camera_state.preview_skeleton->bone_poses().front().local_pose.y -= 125.0f;
    camera_state.preview_skeleton->update_world_transforms();
    const auto moved_pose_layout = build_viewport_layout(
        camera_state,
        canvas_origin,
        canvas_size);
    if (!moved_pose_layout.has_value() ||
        camera_state.viewport_camera.world_center_x != stable_camera.world_center_x ||
        camera_state.viewport_camera.world_center_y != stable_camera.world_center_y ||
        moved_pose_layout->pixels_per_unit != stable_pixels_per_unit ||
        squared_distance(root_screen_before, moved_pose_layout->bones.front().screen_position) <
            100.0f) {
        std::cerr << "Viewport camera changed its framing when only the preview pose changed.\n";
        return false;
    }

    if (!frame_viewport_camera_to_preview_pose(&camera_state)) {
        std::cerr << "Viewport camera smoke could not explicitly fit the moved pose.\n";
        return false;
    }
    const auto fitted_layout = build_viewport_layout(
        camera_state,
        canvas_origin,
        canvas_size);
    if (!fitted_layout.has_value() ||
        camera_state.viewport.pan_x != 0.0 ||
        camera_state.viewport.pan_y != 0.0 ||
        camera_state.viewport.zoom != 1.0 ||
        (std::abs(camera_state.viewport_camera.world_center_x - stable_camera.world_center_x) <
             25.0 &&
         std::abs(camera_state.viewport_camera.world_center_y - stable_camera.world_center_y) <
             25.0)) {
        std::cerr << "Viewport Fit did not replace the stable frame with the current pose.\n";
        return false;
    }

    return true;
}

bool validate_viewport_snap_smoke(const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) || state.preview_skeleton == nullptr ||
        state.session.runtime_data() == nullptr ||
        !scrub_timeline_time(&state, 0.333, "Viewport snap smoke", false)) {
        std::cerr << "Viewport snap smoke could not load its animation context.\n";
        return false;
    }

    const std::string snap_baseline_snapshot =
        marrow::editor::serialize_project(*state.session.project());
    const auto runtime_before_toggle = state.session.runtime_data();
    if (!apply_snap_setting_edit(
            &state,
            "Enable world-grid snapping",
            "viewport:snap:world-enabled",
            [](marrow::editor::ProjectSnapSettings* value) {
                value->world_grid_enabled = true;
            }) ||
        state.session.undo_count() != 1U ||
        state.session.runtime_data() != runtime_before_toggle ||
        !state.session.project()->snap_settings.has_value() ||
        !state.session.project()->snap_settings->world_grid_enabled ||
        !state.session.undo() ||
        marrow::editor::serialize_project(*state.session.project()) !=
            snap_baseline_snapshot) {
        std::cerr << "Snap checkbox was not one project-only undo item.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();

    const std::string magnetic_baseline =
        marrow::editor::serialize_project(*state.session.project());
    const auto* runtime_before_magnetic = state.session.runtime_data();
    const bool dirty_before_magnetic = state.session.dirty();
    const std::uint64_t project_revision_before_magnetic =
        state.session.project_revision();
    const std::uint64_t runtime_revision_before_magnetic =
        state.session.runtime_revision();
    const std::uint64_t preview_revision_before_magnetic =
        state.session.preview_revision();
    if (!apply_snap_setting_edit(
            &state,
            "Enable magnetic vertex snapping",
            "viewport:snap:magnetic-vertex-enabled",
            [](marrow::editor::ProjectSnapSettings* value) {
                value->magnetic_vertex_enabled = true;
            }) ||
        !state.session.project()->snap_settings.has_value() ||
        !state.session.project()->snap_settings->magnetic_vertex_enabled ||
        state.session.undo_count() != 1U ||
        state.session.runtime_data() != runtime_before_magnetic ||
        state.session.project_revision() != project_revision_before_magnetic + 1U ||
        state.session.runtime_revision() != runtime_revision_before_magnetic ||
        state.session.preview_revision() != preview_revision_before_magnetic) {
        std::cerr << "Magnetic vertex checkbox was not one project-only edit.\n";
        return false;
    }
    const std::string magnetic_after =
        marrow::editor::serialize_project(*state.session.project());
    const std::size_t magnetic_undo = state.session.undo_count();
    const std::uint64_t magnetic_project_revision =
        state.session.project_revision();
    if (apply_snap_setting_edit(
            &state,
            "Enable magnetic vertex snapping",
            "viewport:snap:magnetic-vertex-enabled",
            [](marrow::editor::ProjectSnapSettings* value) {
                value->magnetic_vertex_enabled = true;
            }) ||
        marrow::editor::serialize_project(*state.session.project()) != magnetic_after ||
        state.session.undo_count() != magnetic_undo ||
        state.session.project_revision() != magnetic_project_revision ||
        state.session.runtime_revision() != runtime_revision_before_magnetic ||
        state.session.preview_revision() != preview_revision_before_magnetic ||
        !state.session.undo() ||
        marrow::editor::serialize_project(*state.session.project()) !=
            magnetic_baseline ||
        state.session.dirty() != dirty_before_magnetic) {
        std::cerr << "Magnetic vertex same-value edit or undo changed unrelated state.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();

    constexpr ImGuiID kSnapStepItem = 0x4d415235U;
    const auto runtime_before_step_drag = state.session.runtime_data();
    if (!apply_coalesced_snap_setting_edit(
            &state,
            CoalescedEditFrame{kSnapStepItem, true, true, false, false},
            "Update world-grid snap step",
            "viewport:snap:world-step",
            [](marrow::editor::ProjectSnapSettings* value) {
                value->world_grid_step = 12.0;
            }) ||
        !apply_coalesced_snap_setting_edit(
            &state,
            CoalescedEditFrame{kSnapStepItem, false, true, false, false},
            "Update world-grid snap step",
            "viewport:snap:world-step",
            [](marrow::editor::ProjectSnapSettings* value) {
                value->world_grid_step = 12.5;
            }) ||
        !apply_coalesced_snap_setting_edit(
            &state,
            CoalescedEditFrame{kSnapStepItem, false, false, true, true},
            "Update world-grid snap step",
            "viewport:snap:world-step",
            [](marrow::editor::ProjectSnapSettings*) {}) ||
        state.session.undo_count() != 1U ||
        state.session.runtime_data() != runtime_before_step_drag ||
        !state.session.project()->snap_settings.has_value() ||
        std::abs(state.session.project()->snap_settings->world_grid_step - 12.5) > 1e-9 ||
        !state.session.undo() ||
        marrow::editor::serialize_project(*state.session.project()) !=
            snap_baseline_snapshot) {
        std::cerr << "Snap numeric drag was not one coalesced project-only undo item.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();

    constexpr ImGuiID kBlockingEditItem = 0x424c4f43U;
    constexpr ImGuiID kBlockedSnapItem = 0x534e4150U;
    const std::string blocked_snap_before =
        marrow::editor::serialize_project(*state.session.project());
    const auto blocked_runtime_before = state.session.runtime_data();
    const bool blocked_dirty_before = state.session.dirty();
    const std::size_t blocked_undo_before = state.session.undo_count();
    PendingEditAction blocking_edit;
    blocking_edit.item_id = kBlockingEditItem;
    blocking_edit.label = "Blocking authoring gesture";
    blocking_edit.group = "viewport:snap:blocker";
    blocking_edit.impacts = marrow::editor::EditImpact::Project;
    blocking_edit.before_snapshot = capture_history_snapshot(state);
    state.pending_edit_action = std::move(blocking_edit);
    const bool blocked_activation = !apply_coalesced_snap_setting_edit(
        &state,
        CoalescedEditFrame{kBlockedSnapItem, true, true, false, false},
        "Blocked snap step",
        "viewport:snap:blocked-step",
        [](marrow::editor::ProjectSnapSettings* value) {
            value->local_angle_step_degrees = 33.0;
        });
    const bool blocked_continuation = !apply_coalesced_snap_setting_edit(
        &state,
        CoalescedEditFrame{kBlockedSnapItem, false, true, false, false},
        "Blocked snap step",
        "viewport:snap:blocked-step",
        [](marrow::editor::ProjectSnapSettings* value) {
            value->local_angle_step_degrees = 33.0;
        });
    if (!blocked_activation || !blocked_continuation ||
        marrow::editor::serialize_project(*state.session.project()) !=
            blocked_snap_before ||
        state.session.runtime_data() != blocked_runtime_before ||
        state.session.dirty() != blocked_dirty_before ||
        state.session.undo_count() != blocked_undo_before) {
        std::cerr << "Blocked snap drag continuation mutated project state or history.\n";
        return false;
    }
    if (!cancel_coalesced_edit(&state)) {
        return false;
    }

    marrow::editor::ProjectSnapSettings settings;
    settings.world_grid_enabled = true;
    settings.local_angle_enabled = true;
    settings.absolute_scale_enabled = true;
    const auto runtime_before_settings = state.session.runtime_data();
    auto settings_transaction = state.session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Configure viewport snapping",
        "viewport-snap-settings",
        false,
        marrow::editor::EditImpact::Project});
    if (!settings_transaction) {
        std::cerr << settings_transaction.error()->format();
        return false;
    }
    settings_transaction.project()->snap_settings = settings;
    const auto settings_commit = settings_transaction.commit();
    sync_shell_from_editor_session(&state);
    if (!settings_commit || state.session.undo_count() != 1U ||
        state.session.runtime_data() != runtime_before_settings) {
        std::cerr << "Snap settings were not one project-only transaction.\n";
        return false;
    }
    state.session.clear_history();

    const auto arm_index = state.session.runtime_data()->find_bone_index("arm_l");
    const auto root_index = state.session.runtime_data()->find_bone_index("root");
    if (!arm_index.has_value() || !root_index.has_value()) {
        std::cerr << "Viewport snap smoke could not find root/arm_l.\n";
        return false;
    }
    const auto parent_index = state.session.runtime_data()->bones()[*arm_index].parent_index;
    if (!parent_index.has_value()) {
        std::cerr << "Viewport snap smoke expected arm_l to have a parent.\n";
        return false;
    }
    const std::string parent_name =
        state.session.runtime_data()->bones()[*parent_index].name;
    auto reflection_transaction = state.session.begin_edit({
        marrow::editor::EditKind::AddKeyframe,
        "Stage reflected parent for snapping",
        "viewport-snap-reflection",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!reflection_transaction) {
        std::cerr << reflection_transaction.error()->format();
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        *reflection_transaction.project(),
        *state.session.runtime_data(),
        "idle",
        parent_name,
        marrow::editor::TransformTimelineChannel::Scale,
        0.333,
        marrow::editor::TransformKeyframePatch{
            std::nullopt, -1.25, 0.75});
    if (!reflection_transaction.refresh_runtime() ||
        !reflection_transaction.commit()) {
        std::cerr << "Viewport snap smoke could not stage a reflected parent.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();

    const auto reflected_arm_world =
        state.preview_skeleton->bone_world_transforms()[*arm_index];
    const ViewportWorldPoint staged_off_grid_world{
        std::round(static_cast<double>(reflected_arm_world.world_x) / 10.0) * 10.0 +
            3.0,
        static_cast<double>(reflected_arm_world.world_y)};
    const auto staged_off_grid_local =
        viewport_interaction::local_position_for_world_target(
            *state.preview_skeleton, *arm_index, staged_off_grid_world);
    auto off_grid_translate_transaction = state.session.begin_edit({
        marrow::editor::EditKind::AddKeyframe,
        "Stage off-grid constrained translation",
        "viewport-snap-axis-noop",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!staged_off_grid_local.has_value() || !off_grid_translate_transaction) {
        std::cerr << "Viewport snap smoke could not stage an off-grid translation.\n";
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        *off_grid_translate_transaction.project(),
        *state.session.runtime_data(),
        "idle",
        "arm_l",
        marrow::editor::TransformTimelineChannel::Translate,
        0.333,
        marrow::editor::TransformKeyframePatch{
            std::nullopt,
            staged_off_grid_local->x,
            staged_off_grid_local->y});
    if (!off_grid_translate_transaction.refresh_runtime() ||
        !off_grid_translate_transaction.commit()) {
        std::cerr << "Viewport snap smoke could not commit an off-grid translation.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();

    const ImVec2 canvas_origin(23.0f, 41.0f);
    const ImVec2 canvas_size(1000.0f, 700.0f);
    select_bone(&state, *arm_index, "Snap smoke", false);
    auto layout = build_viewport_layout(state, canvas_origin, canvas_size);
    if (!layout.has_value()) {
        return false;
    }
    const auto grid_spacing = viewport_grid_spacing_pixels(state, *layout);
    const double visible_world_step = grid_spacing.has_value()
        ? static_cast<double>(*grid_spacing) /
            static_cast<double>(layout->pixels_per_unit)
        : 0.0;
    if (!grid_spacing.has_value() ||
        std::abs(
            visible_world_step / settings.world_grid_step -
            std::round(visible_world_step / settings.world_grid_step)) > 1e-6) {
        std::cerr << std::setprecision(17)
                  << "Visible grid was not an integer multiple of the snap grid: spacing="
                  << grid_spacing.value_or(0.0f)
                  << " pixels_per_unit=" << layout->pixels_per_unit
                  << " world_step=" << visible_world_step
                  << " ratio=" << visible_world_step / settings.world_grid_step
                  << ".\n";
        return false;
    }
    const auto arm_start =
        state.preview_skeleton->bone_world_transforms()[*arm_index];
    const ImVec2 translate_start = screen_from_world(
        *layout, arm_start.world_x, arm_start.world_y);
    const std::string constrained_click_before =
        marrow::editor::serialize_project(*state.session.project());
    const std::size_t constrained_click_undo_before = state.session.undo_count();
    if (!viewport_interaction::begin_translate_gesture(
            &state, *layout, ViewportTranslateAxis::X, translate_start) ||
        !viewport_interaction::update_translate_gesture(
            &state,
            *layout,
            ImVec2(translate_start.x, translate_start.y - 50.0f),
            ViewportSnapModifiers{})) {
        std::cerr << "Constrained translation no-op regression could not run.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&state, true);
    const auto constrained_click_world =
        state.preview_skeleton->bone_world_transforms()[*arm_index];
    if (marrow::editor::serialize_project(*state.session.project()) !=
            constrained_click_before ||
        state.session.undo_count() != constrained_click_undo_before ||
        std::abs(constrained_click_world.world_x - arm_start.world_x) > 2e-3 ||
        std::abs(constrained_click_world.world_y - arm_start.world_y) > 2e-3) {
        std::cerr << "Orthogonal pointer motion snapped an axis-constrained translation.\n";
        return false;
    }
    const ViewportWorldPoint raw_translate_target{
        static_cast<double>(arm_start.world_x) + 13.2,
        static_cast<double>(arm_start.world_y) - 6.4};
    const ViewportWorldPoint snapped_translate_target{
        std::round(raw_translate_target.x / 10.0) * 10.0,
        std::round(raw_translate_target.y / 10.0) * 10.0};
    const ImVec2 translate_pointer = screen_from_world(
        *layout, raw_translate_target.x, raw_translate_target.y);
    const std::string translate_before =
        marrow::editor::serialize_project(*state.session.project());
    const std::size_t translate_undo_before = state.session.undo_count();
    const bool translate_dirty_before = state.session.dirty();
    if (!viewport_interaction::begin_translate_gesture(
            &state, *layout, ViewportTranslateAxis::Free, translate_start) ||
        !viewport_interaction::update_translate_gesture(
            &state, *layout, translate_pointer, ViewportSnapModifiers{})) {
        std::cerr << "Configured translate snapping did not begin.\n";
        return false;
    }
    auto arm_world = state.preview_skeleton->bone_world_transforms()[*arm_index];
    if (std::abs(arm_world.world_x - snapped_translate_target.x) > 2e-3 ||
        std::abs(arm_world.world_y - snapped_translate_target.y) > 2e-3 ||
        !viewport_interaction::update_translate_gesture(
            &state,
            *layout,
            translate_pointer,
            ViewportSnapModifiers{false, true})) {
        std::cerr << "World snap was not applied before reflected-parent inversion.\n";
        return false;
    }
    arm_world = state.preview_skeleton->bone_world_transforms()[*arm_index];
    if (std::abs(arm_world.world_x - raw_translate_target.x) > 2e-3 ||
        std::abs(arm_world.world_y - raw_translate_target.y) > 2e-3) {
        std::cerr << "Live Alt did not bypass configured translation snapping.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&state, false);
    if (marrow::editor::serialize_project(*state.session.project()) != translate_before ||
        state.session.undo_count() != translate_undo_before ||
        state.session.dirty() != translate_dirty_before) {
        std::cerr << "Transient Alt translation changed project/history state.\n";
        return false;
    }

    auto disable_world_transaction = state.session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Disable configured world snapping",
        "viewport-snap-settings",
        false,
        marrow::editor::EditImpact::Project});
    if (!disable_world_transaction ||
        !disable_world_transaction.project()->snap_settings.has_value()) {
        return false;
    }
    disable_world_transaction.project()->snap_settings->world_grid_enabled = false;
    if (!disable_world_transaction.commit()) {
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();
    select_bone(&state, *arm_index, "Snap smoke", false);
    layout = build_viewport_layout(state, canvas_origin, canvas_size);
    if (!layout.has_value()) {
        return false;
    }
    const auto temporary_start_world =
        state.preview_skeleton->bone_world_transforms()[*arm_index];
    const ImVec2 temporary_start = screen_from_world(
        *layout, temporary_start_world.world_x, temporary_start_world.world_y);
    const ViewportWorldPoint temporary_raw_target{
        static_cast<double>(temporary_start_world.world_x) + 13.2,
        static_cast<double>(temporary_start_world.world_y) - 6.4};
    const ImVec2 temporary_pointer = screen_from_world(
        *layout, temporary_raw_target.x, temporary_raw_target.y);
    const std::string temporary_before =
        marrow::editor::serialize_project(*state.session.project());
    const std::size_t temporary_undo_before = state.session.undo_count();
    const bool temporary_dirty_before = state.session.dirty();
    if (!viewport_interaction::begin_translate_gesture(
            &state, *layout, ViewportTranslateAxis::Free, temporary_start) ||
        !viewport_interaction::update_translate_gesture(
            &state,
            *layout,
            temporary_pointer,
            ViewportSnapModifiers{true, false})) {
        std::cerr << "Temporary command translation snapping did not begin.\n";
        return false;
    }
    arm_world = state.preview_skeleton->bone_world_transforms()[*arm_index];
    if (std::abs(arm_world.world_x - std::round(temporary_raw_target.x / 10.0) * 10.0) >
            2e-3 ||
        std::abs(arm_world.world_y - std::round(temporary_raw_target.y / 10.0) * 10.0) >
            2e-3 ||
        !viewport_interaction::update_translate_gesture(
            &state, *layout, temporary_pointer, ViewportSnapModifiers{})) {
        std::cerr << "Command modifier did not enable disabled world snapping.\n";
        return false;
    }
    arm_world = state.preview_skeleton->bone_world_transforms()[*arm_index];
    if (std::abs(arm_world.world_x - temporary_raw_target.x) > 2e-3 ||
        std::abs(arm_world.world_y - temporary_raw_target.y) > 2e-3) {
        std::cerr << "Live command release did not restore unsnapped translation.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&state, false);
    if (marrow::editor::serialize_project(*state.session.project()) != temporary_before ||
        state.session.undo_count() != temporary_undo_before ||
        state.session.dirty() != temporary_dirty_before) {
        std::cerr << "Transient command translation changed project/history state.\n";
        return false;
    }

    select_bone(&state, *root_index, "Snap smoke", false);
    layout = build_viewport_layout(state, canvas_origin, canvas_size);
    if (!layout.has_value()) {
        return false;
    }
    const ImVec2 rotation_center = layout->bones[*root_index].screen_position;
    const auto ring_point = [](const ImVec2& center, double degrees) {
        const double radians = degrees * static_cast<double>(kPi) / 180.0;
        return ImVec2(
            center.x + static_cast<float>(58.0 * std::cos(radians)),
            center.y - static_cast<float>(58.0 * std::sin(radians)));
    };
    const std::string rotation_before =
        marrow::editor::serialize_project(*state.session.project());
    if (!viewport_interaction::begin_rotate_gesture(
            &state, *layout, ring_point(rotation_center, 0.0))) {
        return false;
    }
    const auto* rotation_start = std::get_if<ViewportRotateGesturePayload>(
        &state.viewport_transform_gesture->payload);
    const double raw_rotation = rotation_start != nullptr
        ? rotation_start->start_absolute_rotation + 22.0
        : std::numeric_limits<double>::quiet_NaN();
    const double snapped_rotation = std::round(raw_rotation / 15.0) * 15.0;
    if (rotation_start == nullptr ||
        !viewport_interaction::update_rotate_gesture(
            &state, *layout, ring_point(rotation_center, 22.0), ViewportSnapModifiers{})) {
        return false;
    }
    const auto current_rotation = [&]() {
        const auto* payload = std::get_if<ViewportRotateGesturePayload>(
            &state.viewport_transform_gesture->payload);
        return payload != nullptr
            ? payload->current_absolute_rotation
            : std::numeric_limits<double>::quiet_NaN();
    };
    if (std::abs(current_rotation() - snapped_rotation) > 1e-4 ||
        !viewport_interaction::update_rotate_gesture(
            &state,
            *layout,
            ring_point(rotation_center, 22.0),
            ViewportSnapModifiers{false, true}) ||
        std::abs(current_rotation() - raw_rotation) > 1e-4 ||
        !viewport_interaction::update_rotate_gesture(
            &state, *layout, ring_point(rotation_center, 22.0), ViewportSnapModifiers{}) ||
        std::abs(current_rotation() - snapped_rotation) > 1e-4) {
        std::cerr << "Rotation did not snap raw absolute angles or sample Alt live.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&state, true);
    if (state.session.undo_count() != 1U ||
        !state.session.undo() ||
        marrow::editor::serialize_project(*state.session.project()) != rotation_before) {
        std::cerr << "Snapped rotation did not commit as one undo item.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();

    auto off_grid_scale_transaction = state.session.begin_edit({
        marrow::editor::EditKind::AddKeyframe,
        "Stage off-grid scale for click regression",
        "viewport-snap-scale-click",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!off_grid_scale_transaction) {
        std::cerr << off_grid_scale_transaction.error()->format();
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        *off_grid_scale_transaction.project(),
        *state.session.runtime_data(),
        "idle",
        "root",
        marrow::editor::TransformTimelineChannel::Scale,
        0.333,
        marrow::editor::TransformKeyframePatch{
            std::nullopt, 1.03, 1.0});
    if (!off_grid_scale_transaction.refresh_runtime() ||
        !off_grid_scale_transaction.commit()) {
        std::cerr << "Viewport snap smoke could not stage an off-grid scale.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();

    select_bone(&state, *root_index, "Snap smoke", false);
    layout = build_viewport_layout(state, canvas_origin, canvas_size);
    const auto scale_basis = layout.has_value()
        ? viewport_interaction::scale_basis(*state.preview_skeleton, *root_index)
        : std::nullopt;
    if (!layout.has_value() || !scale_basis.has_value()) {
        return false;
    }
    const ImVec2 scale_center = screen_from_world(
        *layout, scale_basis->pivot_world.x, scale_basis->pivot_world.y);
    const auto scale_point = [&](double projection) {
        return ImVec2(
            scale_center.x + scale_basis->positive_x_screen_direction.x *
                static_cast<float>(projection),
            scale_center.y + scale_basis->positive_x_screen_direction.y *
                static_cast<float>(projection));
    };
    const ImVec2 scale_start = scale_point(74.0);
    const ImVec2 signed_scale_pointer = scale_point(-11.84);
    const std::string scale_click_before =
        marrow::editor::serialize_project(*state.session.project());
    const std::size_t scale_click_undo_before = state.session.undo_count();
    if (!viewport_interaction::begin_scale_gesture(
            &state, *layout, ViewportScaleHandle::X, scale_start) ||
        !viewport_interaction::update_scale_gesture(
            &state, *layout, scale_start, ViewportSnapModifiers{})) {
        std::cerr << "Off-grid scale click regression could not run.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&state, true);
    if (marrow::editor::serialize_project(*state.session.project()) !=
            scale_click_before ||
        state.session.undo_count() != scale_click_undo_before) {
        std::cerr << "Off-grid scale click materialized a snapped key or history.\n";
        return false;
    }

    const std::string scale_before =
        marrow::editor::serialize_project(*state.session.project());
    if (!viewport_interaction::begin_scale_gesture(
            &state, *layout, ViewportScaleHandle::X, scale_start)) {
        return false;
    }
    const auto* scale_start_payload = std::get_if<ViewportScaleGesturePayload>(
        &state.viewport_transform_gesture->payload);
    const auto raw_scale = scale_start_payload != nullptr
        ? viewport_interaction::scale_candidate(
              *scale_start_payload, *layout, signed_scale_pointer)
        : std::nullopt;
    const double expected_signed_scale = raw_scale.has_value()
        ? std::round(raw_scale->scale_x / 0.1) * 0.1
        : std::numeric_limits<double>::quiet_NaN();
    if (!raw_scale.has_value() ||
        !viewport_interaction::update_scale_gesture(
            &state, *layout, signed_scale_pointer, ViewportSnapModifiers{})) {
        return false;
    }
    const auto current_scale_x = [&]() {
        const auto* payload = std::get_if<ViewportScaleGesturePayload>(
            &state.viewport_transform_gesture->payload);
        return payload != nullptr
            ? payload->current_absolute_scale_x
            : std::numeric_limits<double>::quiet_NaN();
    };
    if (std::abs(current_scale_x() - expected_signed_scale) > 1e-6 ||
        !viewport_interaction::update_scale_gesture(
            &state,
            *layout,
            signed_scale_pointer,
            ViewportSnapModifiers{false, true}) ||
        std::abs(current_scale_x() - raw_scale->scale_x) > 1e-6 ||
        !viewport_interaction::update_scale_gesture(
            &state, *layout, signed_scale_pointer, ViewportSnapModifiers{}) ||
        std::abs(current_scale_x() - expected_signed_scale) > 1e-6 ||
        !viewport_interaction::update_scale_gesture(
            &state, *layout, scale_center, ViewportSnapModifiers{}) ||
        current_scale_x() != 0.0 || std::signbit(current_scale_x())) {
        std::cerr << "Scale snap did not preserve sign, live Alt, or exact zero.\n";
        return false;
    }
    viewport_interaction::finish_transform_gesture(&state, true);
    if (state.session.undo_count() != 1U ||
        !state.session.undo() ||
        marrow::editor::serialize_project(*state.session.project()) != scale_before) {
        std::cerr << "Snapped scale did not commit as one undo item.\n";
        return false;
    }

    std::cout << "Project viewport snapping, modifiers, and history validated.\n";
    return true;
}

bool validate_viewport_prepared_scene_renderer_smoke(
    const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) || state.preview_skeleton == nullptr ||
        state.load_result.atlas_data.empty()) {
        std::cerr << "Viewport command smoke could not load the preview scene.\n";
        return false;
    }
    const auto layout = build_viewport_layout(
        state,
        ImVec2(0.0f, 0.0f),
        ImVec2(640.0f, 480.0f));
    if (!layout.has_value()) {
        std::cerr << "Viewport command smoke could not build the camera layout.\n";
        return false;
    }
    const auto scene_result = marrow::renderer::prepare_setup_pose_scene(
        *state.preview_skeleton,
        *state.load_result.atlas_data.front());
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return false;
    }
    const auto commands = marrow::renderer::build_render_command_list(
        *scene_result.scene,
        viewport_projection_matrix(*layout));
    if (!commands || commands.command_list->commands.empty() ||
        commands.command_list->ordered_events.empty() ||
        commands.command_list->bone_palette.empty()) {
        std::cerr << "Viewport command smoke did not produce a complete render list.\n";
        return false;
    }
    ViewportGeometryPass background;
    ViewportGeometryPass foreground;
    build_viewport_background_geometry(state, *layout, {}, &background);
    build_viewport_overlay_geometry(state, *layout, std::nullopt, nullptr, &foreground);
    if (background.line_vertices.empty() && background.triangle_vertices.empty()) {
        std::cerr << "Viewport command smoke did not produce background geometry.\n";
        return false;
    }
    return true;
}

bool validate_selection_set_shell_smoke(ShellState* state) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr ||
        state->preview_skeleton == nullptr || state->session.runtime_data() == nullptr) {
        return false;
    }

    const auto preview_signature = [](const marrow::runtime::Skeleton& skeleton) {
        std::ostringstream stream;
        stream << std::setprecision(std::numeric_limits<double>::max_digits10);
        for (const auto& bone : skeleton.bone_poses()) {
            stream << bone.local_pose.x << ',' << bone.local_pose.y << ','
                   << bone.local_pose.rotation << ',' << bone.local_pose.scale_x << ','
                   << bone.local_pose.scale_y << ',' << bone.local_pose.shear_x << ','
                   << bone.local_pose.shear_y << ',' << static_cast<int>(bone.inherit) << ';';
        }
        for (const auto& world : skeleton.bone_world_transforms()) {
            stream << world.a << ',' << world.b << ',' << world.c << ',' << world.d << ','
                   << world.world_x << ',' << world.world_y << ';';
        }
        for (const auto& slot_state : skeleton.slot_states()) {
            stream << slot_state.attachment_name << ':';
            if (slot_state.attachment_skin_index.has_value()) {
                stream << *slot_state.attachment_skin_index;
            }
            stream << ':' << slot_state.color.r << ',' << slot_state.color.g << ','
                   << slot_state.color.b << ',' << slot_state.color.a << ';';
        }
        for (const auto& deform : skeleton.mesh_deform_states()) {
            stream << deform.attachment_name << ':';
            for (const double offset : deform.vertex_offsets) {
                stream << offset << ',';
            }
            stream << ';';
        }
        return stream.str();
    };

    const std::string project_before =
        marrow::editor::serialize_project(*state->load_result.project);
    const EditorHistorySnapshot shell_preview_before = capture_history_snapshot(*state);
    const std::string runtime_preview_before = preview_signature(*state->preview_skeleton);
    const auto* runtime_data_before = state->session.runtime_data();
    const auto* preview_skeleton_before = state->preview_skeleton;
    const auto* animation_state_before = state->animation_state;
    const bool session_dirty_before = state->session.dirty();
    const bool shell_dirty_before = state->project_dirty;
    const std::size_t undo_before = state->session.undo_count();
    const std::size_t redo_before = state->session.redo_count();
    const std::uint64_t project_revision_before = state->session.project_revision();
    const std::uint64_t runtime_revision_before = state->session.runtime_revision();
    const std::uint64_t preview_revision_before = state->session.preview_revision();

    const auto& skeleton = *state->load_result.skeleton_data;
    if (skeleton.bones().empty() || skeleton.slots().empty()) {
        std::cerr << "SelectionSet shell smoke requires at least one bone and slot.\n";
        return false;
    }
    const std::size_t bone_index =
        skeleton.find_bone_index("arm_l").value_or(0U);
    const std::size_t slot_index = 0U;
    select_bone(state, bone_index, "SelectionSet smoke", false);
    ResolvedSelection resolved = resolve_shell_selection(*state);
    if (state->selection.items().size() != 1U ||
        state->selection.active_bone() == nullptr ||
        state->selection.active_bone()->bone_name != skeleton.bones()[bone_index].name ||
        resolved.active_bone_index != bone_index ||
        resolved.context_bone_index != bone_index ||
        resolved.active_slot_index.has_value() ||
        selected_bone_index(*state) != bone_index ||
        selected_slot_index(*state).has_value() || selected_attachment(*state).has_value() ||
        selected_constraint(*state).has_value()) {
        std::cerr << "Bone SelectionSet compatibility resolution failed.\n";
        return false;
    }

    select_slot(state, slot_index, "SelectionSet smoke", false);
    resolved = resolve_shell_selection(*state);
    const auto slot_attachment = selected_attachment(*state);
    if (state->selection.items().size() != 1U ||
        state->selection.active_slot() == nullptr ||
        state->selection.active_slot()->slot_name != skeleton.slots()[slot_index].name ||
        resolved.active_bone_index.has_value() ||
        resolved.active_slot_index != slot_index ||
        resolved.context_bone_index != skeleton.slots()[slot_index].bone_index ||
        selected_slot_index(*state) != slot_index ||
        selected_bone_index(*state) != skeleton.slots()[slot_index].bone_index ||
        !slot_attachment.has_value()) {
        std::cerr << "Slot SelectionSet compatibility resolution failed.\n";
        return false;
    }

    select_attachment(state, slot_attachment, "SelectionSet smoke", false);
    resolved = resolve_shell_selection(*state);
    const auto* named_attachment = state->selection.active_attachment();
    const auto resolved_attachment = selected_attachment(*state);
    if (named_attachment == nullptr || !slot_attachment->skin_index.has_value() ||
        *slot_attachment->skin_index >= skeleton.skins().size() ||
        named_attachment->slot_name != skeleton.slots()[slot_index].name ||
        named_attachment->skin_name != skeleton.skins()[*slot_attachment->skin_index].name ||
        named_attachment->attachment_name != slot_attachment->attachment_name ||
        !resolved_attachment.has_value() ||
        resolved_attachment->slot_index != slot_attachment->slot_index ||
        resolved_attachment->skin_index != slot_attachment->skin_index ||
        resolved_attachment->attachment_name != slot_attachment->attachment_name ||
        resolved.active_bone_index.has_value() ||
        resolved.active_slot_index != slot_index ||
        !resolved.active_attachment.has_value() ||
        resolved.context_bone_index != skeleton.slots()[slot_index].bone_index ||
        selected_slot_index(*state) != slot_index ||
        selected_bone_index(*state) != skeleton.slots()[slot_index].bone_index) {
        std::cerr << "Attachment SelectionSet name identity resolution failed.\n";
        return false;
    }

    std::optional<marrow::editor::ConstraintSelection> constraint_identity;
    if (!skeleton.ik_constraints().empty()) {
        constraint_identity = marrow::editor::ConstraintSelection{
            ConstraintKind::Ik,
            skeleton.ik_constraints().front().name};
    } else if (!skeleton.path_constraints().empty()) {
        constraint_identity = marrow::editor::ConstraintSelection{
            ConstraintKind::Path,
            skeleton.path_constraints().front().name};
    } else if (!skeleton.transform_constraints().empty()) {
        constraint_identity = marrow::editor::ConstraintSelection{
            ConstraintKind::Transform,
            skeleton.transform_constraints().front().name};
    } else if (!skeleton.physics_constraints().empty()) {
        constraint_identity = marrow::editor::ConstraintSelection{
            ConstraintKind::Physics,
            skeleton.physics_constraints().front().name};
    }
    if (!constraint_identity.has_value()) {
        std::cerr << "SelectionSet shell smoke requires a constraint fixture.\n";
        return false;
    }

    select_constraint(
        state,
        constraint_identity->kind,
        constraint_identity->constraint_name,
        "SelectionSet smoke",
        false);
    resolved = resolve_shell_selection(*state);
    if (state->selection.items().size() != 1U ||
        state->selection.active_constraint() == nullptr ||
        resolved.active_constraint != constraint_identity ||
        resolved.active_bone_index.has_value() ||
        resolved.active_slot_index.has_value() ||
        resolved.context_bone_index.has_value() ||
        selected_constraint(*state) != constraint_identity ||
        selected_bone_index(*state).has_value() || selected_slot_index(*state).has_value() ||
        selected_attachment(*state).has_value()) {
        std::cerr << "Constraint SelectionSet compatibility resolution failed.\n";
        return false;
    }

    const marrow::editor::BoneSelection mixed_bone{skeleton.bones()[bone_index].name};
    const marrow::editor::SlotSelection mixed_slot{skeleton.slots()[slot_index].name};
    const marrow::editor::AttachmentSelection mixed_attachment{
        skeleton.slots()[slot_index].name,
        skeleton.skins()[*slot_attachment->skin_index].name,
        slot_attachment->attachment_name};

    const marrow::editor::SelectionItem hierarchy_root =
        marrow::editor::BoneSelection{"root"};
    const marrow::editor::SelectionItem hierarchy_spine =
        marrow::editor::BoneSelection{"spine"};
    const marrow::editor::SelectionItem hierarchy_bone = mixed_bone;
    const marrow::editor::SelectionItem hierarchy_slot = mixed_slot;
    const marrow::editor::SelectionItem hierarchy_attachment = mixed_attachment;
    const marrow::editor::SelectionItem hierarchy_constraint = *constraint_identity;
    const std::vector<marrow::editor::SelectionItem> hierarchy_visible{
        hierarchy_root,
        hierarchy_spine,
        hierarchy_bone,
        hierarchy_slot,
        hierarchy_attachment};
    const HierarchySelectionModifiers plain_modifiers{};
    const HierarchySelectionModifiers command_modifiers{true, false};
    const HierarchySelectionModifiers shift_modifiers{false, true};
    const HierarchySelectionModifiers command_shift_modifiers{true, true};

    if (!hierarchy_command_modifier(true, false, true) ||
        hierarchy_command_modifier(true, true, false) ||
        !hierarchy_command_modifier(false, true, false) ||
        hierarchy_command_modifier(false, false, true)) {
        std::cerr << "Hierarchy command modifier did not follow macOS Super and non-macOS Ctrl semantics.\n";
        return false;
    }

    state->selection.clear();
    state->hierarchy_selection_anchor.reset();
    state->selected_timeline_track_id = "hierarchy-smoke-track";
    if (!apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_spine,
            plain_modifiers,
            true) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{hierarchy_spine} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_spine ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_spine ||
        state->selected_timeline_track_id.has_value() ||
        state->status_message.find("active bone spine") == std::string::npos ||
        state->status_message.find("1 selected") == std::string::npos) {
        std::cerr << "Plain hierarchy selection did not replace, activate, anchor, clear timeline focus, and report status.\n";
        return false;
    }

    if (!apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_bone,
            command_modifiers,
            false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{hierarchy_spine, hierarchy_bone} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_bone ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_bone ||
        !apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_bone,
            command_modifiers,
            false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{hierarchy_spine} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_spine ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_bone) {
        std::cerr << "Command hierarchy toggle did not add/remove or preserve deterministic active fallback and anchor.\n";
        return false;
    }

    if (!apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_slot,
            shift_modifiers,
            false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{hierarchy_bone, hierarchy_slot} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_slot ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_bone ||
        hierarchy_row_selection_state(*state, hierarchy_bone) !=
            HierarchyRowSelectionState::Selected ||
        hierarchy_row_selection_state(*state, hierarchy_slot) !=
            HierarchyRowSelectionState::Active ||
        hierarchy_row_selection_state(*state, hierarchy_root) !=
            HierarchyRowSelectionState::Unselected) {
        std::cerr << "A toggled-off visible anchor did not drive Shift range selection or distinct row presentation states.\n";
        return false;
    }

    if (!apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_spine,
            plain_modifiers,
            false) ||
        !state->selection.add_range({hierarchy_constraint}, hierarchy_constraint) ||
        !apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_attachment,
            shift_modifiers,
            false)) {
        std::cerr << "Forward hierarchy range selection could not be prepared.\n";
        return false;
    }
    const std::vector<marrow::editor::SelectionItem> visual_order_range{
        hierarchy_spine,
        hierarchy_bone,
        hierarchy_slot,
        hierarchy_attachment};
    if (state->selection.items() != visual_order_range ||
        state->selection.contains(hierarchy_constraint) ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_attachment ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_spine) {
        std::cerr << "Forward Shift range did not replace mixed selection in visual order.\n";
        return false;
    }

    if (!apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_attachment,
            plain_modifiers,
            false) ||
        !apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_spine,
            shift_modifiers,
            false) ||
        state->selection.items() != visual_order_range ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_spine ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_attachment) {
        std::cerr << "Reverse Shift range did not retain top-to-bottom visual storage order.\n";
        return false;
    }

    state->selection.replace(hierarchy_constraint);
    state->hierarchy_selection_anchor.reset();
    if (!apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_spine,
            command_modifiers,
            false) ||
        !apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_attachment,
            command_shift_modifiers,
            true) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{
                hierarchy_constraint,
                hierarchy_spine,
                hierarchy_bone,
                hierarchy_slot,
                hierarchy_attachment} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_attachment ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_spine ||
        hierarchy_row_selection_state(*state, hierarchy_spine) !=
            HierarchyRowSelectionState::Selected ||
        hierarchy_row_selection_state(*state, hierarchy_attachment) !=
            HierarchyRowSelectionState::Active ||
        hierarchy_row_selection_state(*state, hierarchy_root) !=
            HierarchyRowSelectionState::Unselected ||
        state->status_message.find("active attachment ") == std::string::npos ||
        state->status_message.find("5 selected") == std::string::npos) {
        std::cerr << "Command+Shift hierarchy range did not preserve mixed insertion order, activate the click, or classify rows.\n";
        return false;
    }

    state->selection.clear();
    state->selection.add_range(
        {hierarchy_constraint, hierarchy_spine},
        hierarchy_spine);
    state->hierarchy_selection_anchor = hierarchy_spine;
    if (!apply_hierarchy_selection_gesture(
            state,
            hierarchy_visible,
            hierarchy_spine,
            command_modifiers,
            true) ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_constraint ||
        state->status_message.find(
            std::string("active ") +
            constraint_kind_label(constraint_identity->kind) +
            " constraint " + constraint_identity->constraint_name) ==
            std::string::npos) {
        std::cerr << "Hierarchy toggle fallback status did not report the exact active Constraint identity.\n";
        return false;
    }

    const std::vector<marrow::editor::SelectionItem> collapsed_visible{
        hierarchy_root,
        hierarchy_spine,
        hierarchy_slot,
        hierarchy_attachment};
    if (!apply_hierarchy_selection_gesture(
            state,
            collapsed_visible,
            hierarchy_spine,
            plain_modifiers,
            false) ||
        !apply_hierarchy_selection_gesture(
            state,
            collapsed_visible,
            hierarchy_attachment,
            shift_modifiers,
            false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{
                hierarchy_spine,
                hierarchy_slot,
                hierarchy_attachment} ||
        state->selection.contains(hierarchy_bone)) {
        std::cerr << "Collapsed or filtered hierarchy rows leaked into a visible Shift range.\n";
        return false;
    }

    state->hierarchy_selection_anchor = hierarchy_slot;
    const std::vector<marrow::editor::SelectionItem> selection_before_hidden_anchor =
        state->selection.items();
    if (reconcile_hierarchy_anchor_visibility(state, collapsed_visible) ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_slot ||
        !reconcile_hierarchy_anchor_visibility(
            state,
            {hierarchy_root, hierarchy_spine}) ||
        state->hierarchy_selection_anchor.has_value() ||
        state->selection.items() != selection_before_hidden_anchor) {
        std::cerr << "Hierarchy visibility reconciliation did not retain a visible anchor or clear only a hidden anchor.\n";
        return false;
    }

    state->selection.replace(hierarchy_constraint);
    state->hierarchy_selection_anchor = hierarchy_attachment;
    const std::vector<marrow::editor::SelectionItem> invalid_anchor_visible{
        hierarchy_root,
        hierarchy_spine,
        hierarchy_slot};
    if (!apply_hierarchy_selection_gesture(
            state,
            invalid_anchor_visible,
            hierarchy_slot,
            shift_modifiers,
            false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{hierarchy_slot} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_slot ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_slot) {
        std::cerr << "Shift with an invalid hierarchy anchor did not fall back to plain replacement.\n";
        return false;
    }

    state->selection.clear();
    state->selection.add_range(
        {hierarchy_constraint, hierarchy_slot},
        hierarchy_constraint);
    state->hierarchy_selection_anchor = hierarchy_attachment;
    if (!apply_hierarchy_selection_gesture(
            state,
            invalid_anchor_visible,
            hierarchy_slot,
            command_shift_modifiers,
            false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{
                hierarchy_constraint,
                hierarchy_slot} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_slot ||
        !state->hierarchy_selection_anchor.has_value() ||
        *state->hierarchy_selection_anchor != hierarchy_slot) {
        std::cerr << "Command+Shift with an invalid anchor did not add-or-activate a single item.\n";
        return false;
    }

    state->hierarchy_selection_anchor = hierarchy_spine;
    select_bone(state, bone_index, "Viewport", false);
    if (state->hierarchy_selection_anchor.has_value()) {
        std::cerr << "Viewport selection did not clear the hierarchy range anchor.\n";
        return false;
    }
    state->hierarchy_selection_anchor = hierarchy_spine;
    select_slot(state, slot_index, "Timeline", false);
    if (state->hierarchy_selection_anchor.has_value()) {
        std::cerr << "Timeline slot selection did not clear the hierarchy range anchor.\n";
        return false;
    }
    ShellState global_timeline_state;
    global_timeline_state.hierarchy_selection_anchor = hierarchy_spine;
    TimelineTrackRow global_timeline_track;
    global_timeline_track.id = "global:events";
    global_timeline_track.label = "Events";
    (void)focus_timeline_track(
        &global_timeline_state,
        global_timeline_track,
        0.0,
        "Timeline",
        false);
    if (global_timeline_state.hierarchy_selection_anchor.has_value()) {
        std::cerr << "Global timeline track selection did not clear the hierarchy range anchor.\n";
        return false;
    }
    state->hierarchy_selection_anchor = hierarchy_spine;
    select_attachment(state, slot_attachment, "Viewport", false);
    if (state->hierarchy_selection_anchor.has_value()) {
        std::cerr << "External attachment selection did not clear the hierarchy range anchor.\n";
        return false;
    }
    state->hierarchy_selection_anchor = hierarchy_spine;
    select_constraint(
        state,
        constraint_identity->kind,
        constraint_identity->constraint_name,
        "Constraints",
        false);
    if (state->hierarchy_selection_anchor.has_value()) {
        std::cerr << "Constraint selection did not clear the hierarchy range anchor.\n";
        return false;
    }

    state->selection.clear();
    state->hierarchy_selection_anchor.reset();
    state->selected_timeline_track_id.reset();
    if (!state->selection.add_range(
            {mixed_bone, *constraint_identity, mixed_slot},
            mixed_slot) ||
        selected_slot_index(*state) != slot_index ||
        selected_bone_index(*state) != skeleton.slots()[slot_index].bone_index ||
        selected_constraint(*state).has_value()) {
        std::cerr << "Mixed SelectionSet did not expose only its active slot.\n";
        return false;
    }
    if (!state->selection.add_range({*constraint_identity}, *constraint_identity) ||
        selected_constraint(*state) != constraint_identity ||
        selected_bone_index(*state).has_value() || selected_slot_index(*state).has_value()) {
        std::cerr << "Mixed SelectionSet did not expose only its active constraint.\n";
        return false;
    }

    const auto* animation = selected_animation(*state);
    if (animation == nullptr) {
        std::cerr << "SelectionSet consumer smoke requires an active animation.\n";
        return false;
    }
    const std::vector<TimelineTrackRow> tracks =
        build_timeline_tracks(skeleton, *animation);
    const auto bone_track = std::find_if(
        tracks.begin(),
        tracks.end(),
        [&](const TimelineTrackRow& track) {
            return track.bone_index == bone_index;
        });
    const auto slot_track = std::find_if(
        tracks.begin(),
        tracks.end(),
        [&](const TimelineTrackRow& track) {
            return track.slot_index == slot_index;
        });
    if (bone_track == tracks.end() || slot_track == tracks.end()) {
        std::cerr << "SelectionSet consumer smoke requires bone and slot timeline rows.\n";
        return false;
    }

    state->selection.replace(mixed_bone);
    resolved = resolve_shell_selection(*state);
    if (!timeline_track_matches_selection(*state, *bone_track) ||
        timeline_track_matches_selection(*state, *slot_track) ||
        !resolved.active_bone_index.has_value()) {
        std::cerr << "Active Bone did not exclusively drive bone consumers.\n";
        return false;
    }

    state->selection.replace(mixed_slot);
    resolved = resolve_shell_selection(*state);
    WeightPaintSelectionContext paint_context =
        resolve_weight_paint_selection_context(*state);
    if (!timeline_track_matches_selection(*state, *slot_track) ||
        timeline_track_matches_selection(*state, *bone_track) ||
        resolved.active_bone_index.has_value() ||
        paint_context.target_slot_index != slot_index ||
        paint_context.influence_bone_index != skeleton.slots()[slot_index].bone_index) {
        std::cerr << "Active Slot did not drive slot context and single-item paint fallback.\n";
        return false;
    }

    state->selection.replace(mixed_attachment);
    resolved = resolve_shell_selection(*state);
    paint_context = resolve_weight_paint_selection_context(*state);
    if (!timeline_track_matches_selection(*state, *slot_track) ||
        timeline_track_matches_selection(*state, *bone_track) ||
        resolved.active_bone_index.has_value() ||
        !paint_context.target_attachment.has_value() ||
        paint_context.target_attachment->slot_index != slot_index ||
        paint_context.influence_bone_index != skeleton.slots()[slot_index].bone_index) {
        std::cerr << "Active Attachment did not drive its slot and paint context.\n";
        return false;
    }

    state->selection.replace(*constraint_identity);
    paint_context = resolve_weight_paint_selection_context(*state);
    if (timeline_track_matches_selection(*state, *bone_track) ||
        timeline_track_matches_selection(*state, *slot_track) ||
        paint_context.target_slot_index.has_value() ||
        paint_context.influence_bone_index.has_value()) {
        std::cerr << "Active Constraint leaked into timeline or weight-paint consumers.\n";
        return false;
    }

    state->selection.clear();
    state->selection.add_range(
        {mixed_slot, mixed_attachment, mixed_bone},
        mixed_bone);
    paint_context = resolve_weight_paint_selection_context(*state);
    if (!paint_context.target_attachment.has_value() ||
        paint_context.target_attachment->attachment_name !=
            mixed_attachment.attachment_name ||
        paint_context.influence_bone_index != bone_index) {
        std::cerr << "Mixed active Bone did not use the last Attachment paint target.\n";
        return false;
    }

    state->selection.add_range({*constraint_identity}, *constraint_identity);
    paint_context = resolve_weight_paint_selection_context(*state);
    if (!paint_context.target_attachment.has_value() ||
        paint_context.influence_bone_index.has_value()) {
        std::cerr << "Active Constraint did not disable mixed Paint/Erase influence.\n";
        return false;
    }

    // Click order must not matter: the same {bone, slot} pair paints with the
    // selected bone whichever of the two was clicked last.
    state->selection.clear();
    state->selection.add_range({mixed_bone, mixed_slot}, mixed_slot);
    paint_context = resolve_weight_paint_selection_context(*state);
    if (paint_context.target_slot_index != slot_index ||
        paint_context.influence_bone_index != bone_index) {
        std::cerr << "Slot-active {bone, slot} selection lost its selected influence bone.\n";
        return false;
    }
    state->selection.clear();
    state->selection.add_range({mixed_slot, mixed_bone}, mixed_bone);
    const WeightPaintSelectionContext reversed_context =
        resolve_weight_paint_selection_context(*state);
    if (reversed_context.influence_bone_index !=
        paint_context.influence_bone_index) {
        std::cerr << "Weight-paint influence bone depended on selection click order.\n";
        return false;
    }

    // MAR-160 point selection shares SelectionSet with the hierarchy and uses
    // the same platform command modifier resolved above.
    state->selection.clear();
    state->selection.add_range(
        {hierarchy_constraint, hierarchy_slot},
        hierarchy_slot);
    state->hierarchy_selection_anchor = hierarchy_slot;
    state->selected_timeline_track_id = "viewport-point-smoke";
    if (!apply_viewport_point_selection_gesture(
            state, hierarchy_bone, false, false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{hierarchy_bone} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_bone ||
        state->hierarchy_selection_anchor.has_value() ||
        state->selected_timeline_track_id.has_value() ||
        !apply_viewport_point_selection_gesture(
            state, hierarchy_slot, true, false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{
                hierarchy_bone, hierarchy_slot} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_slot ||
        !apply_viewport_point_selection_gesture(
            state, hierarchy_slot, true, false) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{hierarchy_bone} ||
        state->selection.active() == nullptr ||
        *state->selection.active() != hierarchy_bone ||
        hierarchy_row_selection_state(*state, hierarchy_bone) !=
            HierarchyRowSelectionState::Active) {
        std::cerr << "Viewport plain/toggle point selection did not synchronize exact hierarchy identities.\n";
        return false;
    }

    const auto viewport_layout = build_viewport_layout(
        *state,
        ImVec2(0.0f, 0.0f),
        ImVec2(1280.0f, 720.0f));
    if (!viewport_layout.has_value() || state->load_result.atlas_data.empty()) {
        std::cerr << "MAR-160 smoke could not build viewport hit geometry.\n";
        return false;
    }

    // Category, distance, and stable-order comparisons are tested separately
    // so an overlap cannot depend on variant iteration or container order.
    const std::vector<ViewportEntityHitCandidate> overlap_candidates{
        {hierarchy_attachment,
         ViewportEntityHitPriority::AttachmentSurface,
         0.0f,
         0U},
        {hierarchy_slot, ViewportEntityHitPriority::SlotHandle, 0.0f, 0U},
        {hierarchy_bone, ViewportEntityHitPriority::BoneBody, 0.0f, 0U},
        {hierarchy_root, ViewportEntityHitPriority::BoneJoint, 0.0f, 0U},
        {hierarchy_constraint,
         ViewportEntityHitPriority::ConstraintTarget,
         100.0f,
         99U}};
    const auto overlap_pick =
        resolve_viewport_entity_hit_candidates(overlap_candidates);
    const auto distance_pick = resolve_viewport_entity_hit_candidates({
        {hierarchy_root, ViewportEntityHitPriority::BoneJoint, 9.0f, 0U},
        {hierarchy_bone, ViewportEntityHitPriority::BoneJoint, 4.0f, 9U}});
    const auto order_pick = resolve_viewport_entity_hit_candidates({
        {hierarchy_root, ViewportEntityHitPriority::BoneJoint, 4.0f, 8U},
        {hierarchy_bone, ViewportEntityHitPriority::BoneJoint, 4.0f, 2U}});
    if (!overlap_pick.has_value() || overlap_pick->item != hierarchy_constraint ||
        !distance_pick.has_value() || distance_pick->item != hierarchy_bone ||
        !order_pick.has_value() || order_pick->item != hierarchy_bone) {
        std::cerr << "Viewport overlap precedence was not category, distance, then stable order.\n";
        return false;
    }

    const auto overlay_before = state->viewport.debug_overlay;
    state->viewport.debug_overlay = {};
    state->viewport.debug_overlay.bones = false;
    const auto scene_result = marrow::renderer::prepare_setup_pose_scene(
        *state->preview_skeleton,
        *state->load_result.atlas_data.front());
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return false;
    }
    const ViewportEntityHitGeometry drawable_geometry =
        build_viewport_entity_hit_geometry(
            *state, *viewport_layout, &*scene_result.scene);
    const auto slot_marker = std::find_if(
        drawable_geometry.circles.begin(),
        drawable_geometry.circles.end(),
        [](const ViewportHitCircle& circle) {
            return circle.marker_shape == ViewportHitMarkerShape::Diamond &&
                std::get_if<marrow::editor::SlotSelection>(
                    &circle.candidate.item) != nullptr;
        });
    if (drawable_geometry.triangles.empty() ||
        slot_marker == drawable_geometry.circles.end() ||
        std::any_of(
            drawable_geometry.triangles.begin(),
            drawable_geometry.triangles.end(),
            [&](const ViewportHitTriangle& triangle) {
                return std::get_if<marrow::editor::AttachmentSelection>(
                           &triangle.candidate.item) == nullptr ||
                    !marrow::editor::selection_item_exists(
                        triangle.candidate.item, skeleton);
            })) {
        std::cerr << "Viewport prepared-region hit geometry lost typed attachment or slot identities.\n";
        return false;
    }
    const auto slot_pick = pick_viewport_entity_at_position(
        *state, *viewport_layout, drawable_geometry, slot_marker->center);
    if (!slot_pick.has_value() ||
        std::get_if<marrow::editor::SlotSelection>(&slot_pick->item) == nullptr) {
        std::cerr << "Viewport slot centroid did not outrank its rendered attachment surface.\n";
        return false;
    }
    ViewportEntityHitGeometry attachment_only_geometry;
    attachment_only_geometry.triangles = drawable_geometry.triangles;
    const auto& triangle = attachment_only_geometry.triangles.front();
    const ImVec2 triangle_center(
        (triangle.points[0].x + triangle.points[1].x + triangle.points[2].x) /
            3.0f,
        (triangle.points[0].y + triangle.points[1].y + triangle.points[2].y) /
            3.0f);
    const auto attachment_pick = pick_viewport_entity_at_position(
        *state,
        *viewport_layout,
        attachment_only_geometry,
        triangle_center);
    if (!attachment_pick.has_value() ||
        std::get_if<marrow::editor::AttachmentSelection>(
            &attachment_pick->item) == nullptr) {
        std::cerr << "Viewport rendered-region triangle was not pickable as an Attachment.\n";
        return false;
    }
    // A degenerate (zero-area) triangle - e.g. an attachment scaled to
    // exactly zero - must not hit-test as covering the entire viewport.
    ViewportEntityHitGeometry degenerate_geometry;
    ViewportHitTriangle degenerate_triangle = drawable_geometry.triangles.front();
    degenerate_triangle.points = {
        ImVec2(120.0f, 90.0f), ImVec2(120.0f, 90.0f), ImVec2(120.0f, 90.0f)};
    degenerate_geometry.triangles.push_back(degenerate_triangle);
    const auto degenerate_pick = pick_viewport_entity_at_position(
        *state, *viewport_layout, degenerate_geometry, ImVec2(400.0f, 300.0f));
    if (degenerate_pick.has_value()) {
        std::cerr << "Viewport degenerate triangle hit-tested as covering the viewport.\n";
        return false;
    }

    state->viewport.debug_overlay.bones = true;
    state->viewport.debug_overlay.ik_constraints = true;
    state->viewport.debug_overlay.path_constraints = true;
    state->viewport.debug_overlay.physics_constraints = true;
    const ViewportEntityHitGeometry constraint_geometry =
        build_viewport_entity_hit_geometry(
            *state, *viewport_layout, nullptr);
    auto constraint_probe = std::optional<ImVec2>{};
    if (const auto marker = std::find_if(
            constraint_geometry.circles.begin(),
            constraint_geometry.circles.end(),
            [](const ViewportHitCircle& circle) {
                return std::get_if<marrow::editor::ConstraintSelection>(
                           &circle.candidate.item) != nullptr;
            });
        marker != constraint_geometry.circles.end()) {
        constraint_probe = marker->center;
    } else if (const auto segment = std::find_if(
                   constraint_geometry.segments.begin(),
                   constraint_geometry.segments.end(),
                   [](const ViewportHitSegment& value) {
                       return std::get_if<marrow::editor::ConstraintSelection>(
                                  &value.candidate.item) != nullptr;
                   });
               segment != constraint_geometry.segments.end()) {
        constraint_probe = ImVec2(
            (segment->start.x + segment->end.x) * 0.5f,
            (segment->start.y + segment->end.y) * 0.5f);
    }
    if (!constraint_probe.has_value()) {
        std::cerr << "Viewport constraint overlays did not expose a pick target.\n";
        return false;
    }
    const auto constraint_pick = pick_viewport_entity_at_position(
        *state,
        *viewport_layout,
        constraint_geometry,
        *constraint_probe);
    if (!constraint_pick.has_value() ||
        std::get_if<marrow::editor::ConstraintSelection>(
            &constraint_pick->item) == nullptr ||
        !marrow::editor::selection_item_exists(
            constraint_pick->item, skeleton)) {
        std::cerr << "Viewport visible constraint target did not outrank overlapping bones.\n";
        return false;
    }

    state->viewport.debug_overlay.bones = true;
    ViewportLayout box_layout = *viewport_layout;
    std::reverse(box_layout.bones.begin(), box_layout.bones.end());
    const auto inactive_node = std::find_if(
        box_layout.bones.begin(),
        box_layout.bones.end(),
        [](const BoneCanvasNode& node) { return node.active; });
    if (inactive_node == box_layout.bones.end()) {
        std::cerr << "Viewport box smoke requires a runtime-active bone.\n";
        return false;
    }
    const std::size_t excluded_bone_index = inactive_node->bone_index;
    inactive_node->active = false;
    const ImVec2 full_box_start(
        box_layout.canvas_origin.x - 1.0f,
        box_layout.canvas_origin.y - 1.0f);
    const ImVec2 full_box_end(
        box_layout.canvas_end.x + 1.0f,
        box_layout.canvas_end.y + 1.0f);
    state->viewport.debug_overlay.bones = false;
    if (!collect_viewport_box_bones(
             *state, box_layout, full_box_start, full_box_end)
             .empty()) {
        std::cerr << "Viewport box selected Bones while the bone overlay was hidden.\n";
        return false;
    }
    state->viewport.debug_overlay.bones = true;
    const std::vector<marrow::editor::SelectionItem> ordered_box_bones =
        collect_viewport_box_bones(
            *state, box_layout, full_box_start, full_box_end);
    if (ordered_box_bones.empty() ||
        !std::is_sorted(
            ordered_box_bones.begin(),
            ordered_box_bones.end(),
            [&](const marrow::editor::SelectionItem& left,
                const marrow::editor::SelectionItem& right) {
                const auto* left_bone =
                    std::get_if<marrow::editor::BoneSelection>(&left);
                const auto* right_bone =
                    std::get_if<marrow::editor::BoneSelection>(&right);
                return skeleton.find_bone_index(left_bone->bone_name) <
                    skeleton.find_bone_index(right_bone->bone_name);
            }) ||
        std::any_of(
            ordered_box_bones.begin(),
            ordered_box_bones.end(),
            [&](const marrow::editor::SelectionItem& item) {
                const auto* bone =
                    std::get_if<marrow::editor::BoneSelection>(&item);
                return bone == nullptr ||
                    skeleton.find_bone_index(bone->bone_name) ==
                        excluded_bone_index;
            })) {
        std::cerr << "Viewport box collection was not active-Bone-only in stable skeleton order.\n";
        return false;
    }

    state->selection.replace(hierarchy_constraint);
    if (!viewport_interaction::begin_box_selection(
            state, full_box_start, false) ||
        !viewport_interaction::update_box_selection(
            state, ImVec2(full_box_start.x + 3.0f, full_box_start.y)) ||
        !state->viewport_box_selection.has_value() ||
        state->viewport_box_selection->dragged ||
        viewport_interaction::finish_box_selection(
            state, box_layout, true) ||
        state->selection.items() !=
            std::vector<marrow::editor::SelectionItem>{hierarchy_constraint}) {
        std::cerr << "Viewport box drag threshold materialized a click/no-movement selection.\n";
        return false;
    }

    state->hierarchy_selection_anchor = hierarchy_slot;
    state->selected_timeline_track_id = "viewport-box-forward";
    if (!viewport_interaction::begin_box_selection(
            state, full_box_start, false) ||
        !viewport_interaction::update_box_selection(state, full_box_end) ||
        !viewport_interaction::finish_box_selection(state, box_layout, true) ||
        state->selection.items() != ordered_box_bones ||
        state->selection.active() == nullptr ||
        *state->selection.active() != ordered_box_bones.back() ||
        state->hierarchy_selection_anchor.has_value() ||
        state->selected_timeline_track_id.has_value()) {
        std::cerr << "Forward viewport box did not replace in skeleton order and synchronize hierarchy context.\n";
        return false;
    }
    state->selection.replace(hierarchy_constraint);
    if (!viewport_interaction::begin_box_selection(
            state, full_box_end, false) ||
        !viewport_interaction::update_box_selection(state, full_box_start) ||
        !viewport_interaction::finish_box_selection(state, box_layout, true) ||
        state->selection.items() != ordered_box_bones) {
        std::cerr << "Reverse viewport box did not match the normalized forward rectangle.\n";
        return false;
    }

    state->selection.clear();
    state->selection.add_range(
        {hierarchy_constraint, hierarchy_slot, ordered_box_bones.front()},
        hierarchy_slot);
    std::vector<marrow::editor::SelectionItem> additive_expected{
        hierarchy_constraint, hierarchy_slot, ordered_box_bones.front()};
    for (const auto& item : ordered_box_bones) {
        if (std::find(additive_expected.begin(), additive_expected.end(), item) ==
            additive_expected.end()) {
            additive_expected.push_back(item);
        }
    }
    if (!viewport_interaction::begin_box_selection(
            state, full_box_start, true) ||
        !viewport_interaction::update_box_selection(state, full_box_end) ||
        !viewport_interaction::finish_box_selection(state, box_layout, true) ||
        state->selection.items() != additive_expected ||
        state->selection.active() == nullptr ||
        *state->selection.active() != ordered_box_bones.back()) {
        std::cerr << "Additive viewport box did not retain its mixed prefix and append missing Bones.\n";
        return false;
    }

    const ImVec2 empty_start(
        box_layout.canvas_end.x + 100.0f,
        box_layout.canvas_end.y + 100.0f);
    const ImVec2 empty_end(empty_start.x + 30.0f, empty_start.y + 30.0f);
    const auto additive_before_empty = state->selection.items();
    state->hierarchy_selection_anchor = hierarchy_slot;
    state->selected_timeline_track_id = "viewport-empty-additive";
    if (!viewport_interaction::begin_box_selection(state, empty_start, true) ||
        !viewport_interaction::update_box_selection(state, empty_end) ||
        viewport_interaction::finish_box_selection(state, box_layout, true) ||
        state->selection.items() != additive_before_empty ||
        state->hierarchy_selection_anchor !=
            std::optional<marrow::editor::SelectionItem>(hierarchy_slot) ||
        state->selected_timeline_track_id != "viewport-empty-additive") {
        std::cerr << "Empty additive viewport box did not remain a strict no-op.\n";
        return false;
    }
    if (!viewport_interaction::begin_box_selection(state, empty_start, false) ||
        !viewport_interaction::update_box_selection(state, empty_end) ||
        !viewport_interaction::finish_box_selection(state, box_layout, true) ||
        !state->selection.items().empty() ||
        state->hierarchy_selection_anchor.has_value() ||
        state->selected_timeline_track_id.has_value()) {
        std::cerr << "Empty plain viewport box did not clear selection and hierarchy context.\n";
        return false;
    }
    state->viewport.debug_overlay = overlay_before;
    state->selection.clear();

    if (marrow::editor::serialize_project(*state->load_result.project) != project_before ||
        !history_snapshots_equal(shell_preview_before, capture_history_snapshot(*state)) ||
        preview_signature(*state->preview_skeleton) != runtime_preview_before ||
        state->session.runtime_data() != runtime_data_before ||
        state->preview_skeleton != preview_skeleton_before ||
        state->animation_state != animation_state_before ||
        state->session.dirty() != session_dirty_before ||
        state->project_dirty != shell_dirty_before ||
        state->session.undo_count() != undo_before ||
        state->session.redo_count() != redo_before ||
        state->session.project_revision() != project_revision_before ||
        state->session.runtime_revision() != runtime_revision_before ||
        state->session.preview_revision() != preview_revision_before) {
        std::cerr << "Transient selection changed project, preview, runtime, history, dirty, or revisions.\n";
        return false;
    }

    return true;
}


bool validate_viewport_selection_smoke(ShellState& shell_state) {
    const auto spine_index = shell_state.load_result.skeleton_data->find_bone_index("spine");
    if (!spine_index.has_value()) {
        std::cerr << "Timeline smoke validation requires the spine bone.\n";
        return false;
    }

    if (!set_selected_animation(&shell_state, "attack", "Smoke", false, true)) {
        std::cerr << "Animation selection smoke validation failed for attack.\n";
        return false;
    }
    if (shell_state.selected_animation_name != "attack") {
        std::cerr << "Animation selection did not update the shell timeline state.\n";
        return false;
    }

    if (const auto arm_index = shell_state.load_result.skeleton_data->find_bone_index("arm_l")) {
        if (!scrub_timeline_time(&shell_state, 0.2, "Smoke", false)) {
            std::cerr << "Timeline scrub smoke validation failed for attack at t=0.2.\n";
            return false;
        }
        const double arm_rotation =
            static_cast<double>(
                shell_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation);
        if (std::abs(arm_rotation - 60.0) > 1e-3) {
            std::cerr << "Timeline scrub did not update the preview arm rotation at t=0.2.\n";
            return false;
        }

        const auto smoke_layout = build_viewport_layout(
            shell_state,
            ImVec2(0.0f, 0.0f),
            ImVec2(1280.0f, 720.0f));
        if (!smoke_layout.has_value()) {
            std::cerr << "Failed to build a viewport layout for headless smoke validation.\n";
            return false;
        }

        const ViewportTextureUv top_left_uv = viewport_texture_uv(true);
        const ViewportTextureUv bottom_left_uv = viewport_texture_uv(false);
        if (top_left_uv.v0 != 0.0f || top_left_uv.v1 != 1.0f ||
            bottom_left_uv.v0 != 1.0f || bottom_left_uv.v1 != 0.0f) {
            std::cerr << "Viewport image UVs do not follow the Sokol texture origin.\n";
            return false;
        }

        const ViewportFramebufferSize initial_framebuffer_size =
            viewport_framebuffer_size(ImVec2(320.0f, 180.0f), ImVec2(2.0f, 2.0f));
        const ViewportFramebufferSize resized_framebuffer_size =
            viewport_framebuffer_size(ImVec2(640.0f, 360.0f), ImVec2(2.0f, 2.0f));
        if (initial_framebuffer_size.width != 640 ||
            initial_framebuffer_size.height != 360 ||
            resized_framebuffer_size.width != 1280 ||
            resized_framebuffer_size.height != 720) {
            std::cerr << "Viewport framebuffer sizing did not scale with the panel extent.\n";
            return false;
        }

        const ImVec2 spine_position = smoke_layout->bones[*spine_index].screen_position;
        const ImVec2 arm_position = smoke_layout->bones[*arm_index].screen_position;
        const ImVec2 bone_vector(
            arm_position.x - spine_position.x,
            arm_position.y - spine_position.y);
        const float bone_length =
            std::sqrt((bone_vector.x * bone_vector.x) + (bone_vector.y * bone_vector.y));
        if (bone_length <= 1e-6f) {
            std::cerr << "Viewport smoke validation could not build a valid spine->arm segment.\n";
            return false;
        }
        const ImVec2 bone_direction(bone_vector.x / bone_length, bone_vector.y / bone_length);
        const ImVec2 bone_perpendicular(-bone_direction.y, bone_direction.x);

        const ImVec2 joint_priority_probe(
            spine_position.x + (bone_direction.x * (kBoneJointHitRadiusPixels - 1.5f)),
            spine_position.y + (bone_direction.y * (kBoneJointHitRadiusPixels - 1.5f)));
        const auto joint_priority_pick =
            pick_bone_at_position(*smoke_layout, joint_priority_probe);
        if (!joint_priority_pick.has_value() || *joint_priority_pick != *spine_index) {
            std::cerr << "Viewport picking did not prioritize the 6px joint hit zone over the bone body.\n";
            return false;
        }

        const ImVec2 segment_midpoint(
            (spine_position.x + arm_position.x) * 0.5f,
            (spine_position.y + arm_position.y) * 0.5f);
        const ImVec2 segment_body_probe(
            segment_midpoint.x + (bone_perpendicular.x * (kBoneBodyHitThresholdPixels - 1.0f)),
            segment_midpoint.y + (bone_perpendicular.y * (kBoneBodyHitThresholdPixels - 1.0f)));
        const auto segment_body_pick =
            pick_bone_at_position(*smoke_layout, segment_body_probe);
        if (!segment_body_pick.has_value() || *segment_body_pick != *arm_index) {
            std::cerr << "Viewport picking did not select the nearest bone within the 8px body threshold.\n";
            return false;
        }

        const ImVec2 segment_miss_probe(
            segment_midpoint.x + (bone_perpendicular.x * (kBoneBodyHitThresholdPixels + 1.0f)),
            segment_midpoint.y + (bone_perpendicular.y * (kBoneBodyHitThresholdPixels + 1.0f)));
        if (pick_bone_at_position(*smoke_layout, segment_miss_probe).has_value()) {
            std::cerr << "Viewport picking accepted a point outside the 8px bone body threshold.\n";
            return false;
        }

        const auto picked_bone = pick_bone_at_position(
            *smoke_layout,
            smoke_layout->bones[*arm_index].screen_position);
        if (!picked_bone.has_value() || *picked_bone != *arm_index) {
            std::cerr << "Viewport selection smoke validation failed for bone arm_l.\n";
            return false;
        }

        select_bone(&shell_state, *picked_bone, "Viewport", false);
        if (!selected_bone_index(shell_state).has_value() ||
            *selected_bone_index(shell_state) != *arm_index) {
            std::cerr << "Viewport selection did not update the inspector selection state.\n";
            return false;
        }

        if (const auto spine_index = shell_state.load_result.skeleton_data->find_bone_index("spine")) {
            select_bone(&shell_state, *spine_index, "Hierarchy", false);
            if (!selected_bone_index(shell_state).has_value() ||
                *selected_bone_index(shell_state) != *spine_index) {
                std::cerr << "Hierarchy selection did not update the inspector selection state.\n";
                return false;
            }
        }
    }
    return true;
}

} // namespace marrow::editor::shell
