#include "shell_inspector.hpp"

#include <algorithm>
#include <cstdio>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "shell_selection.hpp"
#include "shell_theme.hpp"
#include "shell_timeline.hpp"
#include "shell_viewport_ui.hpp"
#include "shell_widgets.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;
using marrow::editor::IconRegistry;

const char* yes_no(bool value);
const char* attachment_kind_name(marrow::runtime::AttachmentKind kind);
const char* sequence_playback_mode_name(
    marrow::runtime::SequencePlaybackMode mode);
void draw_attachment_details(
    const ShellState& state,
    const SlotAttachmentReference& reference) {
    const auto& skeleton = *state.load_result.skeleton_data;
    const auto& slot = skeleton.slots()[reference.slot_index];
    const auto& attachment = *reference.attachment;

    ImGui::Text("Attachment: %s", attachment.name.c_str());
    ImGui::Text("Slot: %s", slot.name.c_str());
    ImGui::Text("Source skin: %s", source_skin_name(skeleton, reference.skin_index).c_str());
    ImGui::Text("Kind: %s", attachment_kind_name(attachment.kind));
    if (!attachment.region_name.empty()) {
        ImGui::Text("Region: %s", attachment.region_name.c_str());
    }

    if (const auto preview_selection = current_attachment_selection(state, reference.slot_index)) {
        ImGui::Text(
            "Preview active: %s",
            yes_no(attachment_matches_selection(*preview_selection, reference)));
    } else {
        ImGui::TextUnformatted("Preview active: no");
    }

    if (attachment.sequence.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Sequence");
        ImGui::Text(
            "Frames: %zu @ %.1f fps",
            attachment.sequence->frame_regions.size(),
            attachment.sequence->fps);
        ImGui::Text(
            "Playback: %s",
            sequence_playback_mode_name(attachment.sequence->playback_mode));
        ImGui::Text("Setup frame: %zu", attachment.sequence->setup_frame);
    }

    if (attachment.mesh_geometry != nullptr) {
        std::size_t weighted_vertices = 0;
        for (const auto& weights : attachment.mesh_geometry->weights) {
            if (!weights.influences.empty()) {
                ++weighted_vertices;
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Mesh Data");
        ImGui::Text("Vertices: %zu", attachment.mesh_geometry->vertices.size() / 2U);
        ImGui::Text("Triangles: %zu", attachment.mesh_geometry->triangles.size() / 3U);
        ImGui::Text(
            "Weighted vertices: %zu / %zu",
            weighted_vertices,
            attachment.mesh_geometry->weights.size());

        const std::vector<MeshWeightVertexRow> weight_rows =
            build_mesh_weight_rows(skeleton, attachment);
        if (!weight_rows.empty() &&
            ImGui::TreeNodeEx("Mesh Weights", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextUnformatted(
                "Vertex-local positions and per-bone bind offsets mirror the exported weighted mesh.");
            ImGui::BeginChild("mesh_weight_rows", ImVec2(0.0f, 180.0f), true);
            for (const MeshWeightVertexRow& row : weight_rows) {
                const std::string header = "Vertex " + std::to_string(row.vertex_index) +
                    "  local(" + std::to_string(row.local_x) + ", " +
                    std::to_string(row.local_y) + ")";
                if (ImGui::TreeNodeEx(
                        header.c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen |
                            (row.influences.empty() ? ImGuiTreeNodeFlags_Leaf : 0))) {
                    if (row.influences.empty()) {
                        ImGui::TextUnformatted("No bone influences.");
                    } else {
                        for (const MeshWeightInfluenceRow& influence : row.influences) {
                            ImGui::BulletText(
                                "%s  bind(%.1f, %.1f)  weight %.3f",
                                influence.bone_name.c_str(),
                                influence.bind_x,
                                influence.bind_y,
                                influence.weight);
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::LinkedMesh &&
        attachment.linked_mesh.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Linked Mesh");
        ImGui::Text(
            "Parent attachment: %s",
            attachment.linked_mesh->parent_attachment.c_str());
        std::string parent_skin = "<current skin>";
        if (attachment.linked_mesh->parent_skin_name.has_value()) {
            parent_skin = *attachment.linked_mesh->parent_skin_name;
        } else if (attachment.linked_mesh->parent_skin_index.has_value()) {
            parent_skin = source_skin_name(skeleton, attachment.linked_mesh->parent_skin_index);
        }
        ImGui::Text("Parent skin: %s", parent_skin.c_str());
        ImGui::Text("Inherit deform: %s", yes_no(attachment.linked_mesh->deform));
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::Point &&
        attachment.point_attachment.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Point");
        ImGui::Text(
            "Local position: (%.1f, %.1f)",
            static_cast<double>(attachment.point_attachment->local_position.x),
            static_cast<double>(attachment.point_attachment->local_position.y));
        ImGui::Text("Rotation: %.1f deg", attachment.point_attachment->rotation);
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::BoundingBox &&
        attachment.bounding_box.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Bounding Box");
        ImGui::Text("Vertices: %zu", attachment.bounding_box->polygon.size());
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::Clipping &&
        attachment.clipping_attachment.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Clipping");
        ImGui::Text("Vertices: %zu", attachment.clipping_attachment->polygon.size());
        ImGui::Text("End slot: %s", attachment.clipping_attachment->end_slot_name.c_str());
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::Path &&
        attachment.path_attachment.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Path");
        ImGui::Text("Control points: %zu", attachment.path_attachment->control_points.size());
    }
}


std::string parent_bone_name(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::BoneData& bone) {
    if (!bone.parent_index.has_value() || *bone.parent_index >= skeleton.bones().size()) {
        return "<root>";
    }
    return skeleton.bones()[*bone.parent_index].name;
}

std::string join_slots_for_bone(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t bone_index) {
    std::vector<std::string> slot_names;
    for (const auto& slot : skeleton.slots()) {
        if (slot.bone_index == bone_index) {
            slot_names.push_back(slot.name);
        }
    }
    return join_strings(slot_names);
}


const char* yes_no(bool value) {
    return value ? "yes" : "no";
}

const char* blend_mode_name(marrow::runtime::BlendMode blend_mode) {
    switch (blend_mode) {
    case marrow::runtime::BlendMode::Normal:
        return "normal";
    case marrow::runtime::BlendMode::Additive:
        return "additive";
    case marrow::runtime::BlendMode::Multiply:
        return "multiply";
    case marrow::runtime::BlendMode::Screen:
        return "screen";
    }

    return "unknown";
}

const char* attachment_kind_name(marrow::runtime::AttachmentKind kind) {
    switch (kind) {
    case marrow::runtime::AttachmentKind::Region:
        return "region";
    case marrow::runtime::AttachmentKind::Mesh:
        return "mesh";
    case marrow::runtime::AttachmentKind::LinkedMesh:
        return "linked mesh";
    case marrow::runtime::AttachmentKind::Point:
        return "point";
    case marrow::runtime::AttachmentKind::BoundingBox:
        return "bounding box";
    case marrow::runtime::AttachmentKind::Clipping:
        return "clipping";
    case marrow::runtime::AttachmentKind::Path:
        return "path";
    }

    return "unknown";
}

const char* sequence_playback_mode_name(
    marrow::runtime::SequencePlaybackMode mode) {
    switch (mode) {
    case marrow::runtime::SequencePlaybackMode::Hold:
        return "hold";
    case marrow::runtime::SequencePlaybackMode::Once:
        return "once";
    case marrow::runtime::SequencePlaybackMode::Loop:
        return "loop";
    case marrow::runtime::SequencePlaybackMode::PingPong:
        return "ping-pong";
    case marrow::runtime::SequencePlaybackMode::OnceReverse:
        return "once reverse";
    case marrow::runtime::SequencePlaybackMode::LoopReverse:
        return "loop reverse";
    case marrow::runtime::SequencePlaybackMode::PingPongReverse:
        return "ping-pong reverse";
    }

    return "unknown";
}


std::string format_slot_color(const marrow::runtime::SlotColor& color) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2)
           << "(" << color.r << ", "
           << color.g << ", "
           << color.b << ", "
           << color.a << ")";
    return stream.str();
}


std::vector<MeshWeightVertexRow> build_mesh_weight_rows(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AttachmentData& attachment) {
    std::vector<MeshWeightVertexRow> rows;
    if (attachment.mesh_geometry == nullptr) {
        return rows;
    }

    const auto& geometry = *attachment.mesh_geometry;
    const std::size_t vertex_count =
        std::min(geometry.vertices.size() / 2U, geometry.weights.size());
    rows.reserve(vertex_count);

    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        MeshWeightVertexRow row;
        row.vertex_index = vertex_index;
        row.local_x = geometry.vertices[(vertex_index * 2U)];
        row.local_y = geometry.vertices[(vertex_index * 2U) + 1U];
        row.influences.reserve(geometry.weights[vertex_index].influences.size());

        for (const auto& influence : geometry.weights[vertex_index].influences) {
            const std::string bone_name =
                influence.bone_index < skeleton.bones().size()
                ? skeleton.bones()[influence.bone_index].name
                : ("<bone " + std::to_string(influence.bone_index) + ">");
            row.influences.push_back(MeshWeightInfluenceRow{
                bone_name,
                influence.x,
                influence.y,
                influence.weight});
        }

        rows.push_back(std::move(row));
    }

    return rows;
}

bool inspector_bone_pose_editable(const ShellState& state) noexcept {
    return static_cast<bool>(state.load_result) &&
        state.shell_mode == ShellMode::Animation &&
        !state.selected_animation_name.empty() &&
        !state.weight_paint.enabled;
}

namespace {

std::string_view inspector_transform_channel_name(
    marrow::editor::TransformTimelineChannel channel) {
    switch (channel) {
    case marrow::editor::TransformTimelineChannel::Rotate:
        return "rotation";
    case marrow::editor::TransformTimelineChannel::Translate:
        return "translation";
    case marrow::editor::TransformTimelineChannel::Scale:
        return "scale";
    case marrow::editor::TransformTimelineChannel::Shear:
        return "shear";
    }
    return "transform";
}

void finish_inspector_transform_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->inspector_transform_gesture.has_value()) {
        return;
    }

    InspectorTransformGesture gesture =
        std::move(*state->inspector_transform_gesture);
    state->inspector_transform_gesture.reset();
    const std::string channel_name(
        inspector_transform_channel_name(gesture.channel));
    if (!commit || !gesture.changed) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        if (commit) {
            state->status_message = "No " + channel_name + " key change";
        }
        return;
    }

    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message = "Bone " + channel_name + " edit failed";
        return;
    }
    state->error_message.clear();
    state->status_message = "Keyed bone " + channel_name + " at " +
        format_time_seconds(state->timeline_time_seconds);
}

bool apply_inspector_transform_drag(
    ShellState* state,
    bool value_changed,
    std::size_t bone_index,
    marrow::editor::TransformTimelineChannel channel,
    const marrow::editor::TransformKeyframePatch& patch) {
    if (state == nullptr || !inspector_bone_pose_editable(*state) ||
        state->load_result.project == nullptr ||
        bone_index >= state->load_result.skeleton_data->bones().size()) {
        return false;
    }

    const ImGuiID item_id = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) {
        if (state->pending_edit_action.has_value() ||
            state->weight_paint_stroke.active) {
            state->status_message = "Finish the active edit before editing a bone pose";
            return false;
        }
        state->timeline_playing = false;
        state->session.set_playing(false);
        const std::string channel_name(inspector_transform_channel_name(channel));
        auto transaction = state->session.begin_edit({
            marrow::editor::EditKind::AddKeyframe,
            "Key bone " + channel_name,
            "inspector-transform:" + state->selected_animation_name + ":" +
                state->load_result.skeleton_data->bones()[bone_index].name + ":" +
                channel_name,
            false,
            marrow::editor::EditImpact::Project |
                marrow::editor::EditImpact::Runtime |
                marrow::editor::EditImpact::Preview});
        if (!transaction) {
            state->error_message = transaction.error()->format();
            return false;
        }
        InspectorTransformGesture gesture;
        gesture.item_id = item_id;
        gesture.channel = channel;
        gesture.transaction = std::move(transaction);
        state->inspector_transform_gesture.emplace(std::move(gesture));
    }

    if (!state->inspector_transform_gesture.has_value() ||
        state->inspector_transform_gesture->item_id != item_id) {
        return false;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        finish_inspector_transform_gesture(state, false);
        state->status_message = "Cancelled bone transform edit";
        return true;
    }

    if (value_changed) {
        auto& gesture = *state->inspector_transform_gesture;
        marrow::editor::upsert_transform_keyframe(
            *gesture.transaction.project(),
            *state->session.runtime_data(),
            state->selected_animation_name,
            state->load_result.skeleton_data->bones()[bone_index].name,
            channel,
            state->timeline_time_seconds,
            patch);
        const marrow::editor::SessionResult refresh =
            gesture.transaction.refresh_runtime();
        if (!refresh) {
            const std::string error = refresh.error->format();
            finish_inspector_transform_gesture(state, false);
            state->error_message = error;
            state->status_message = "Bone transform preview failed";
            return false;
        }
        gesture.changed = true;
        sync_shell_from_editor_session(state);
    }

    if (ImGui::IsItemDeactivatedAfterEdit()) {
        finish_inspector_transform_gesture(state, true);
    } else if (ImGui::IsItemDeactivated()) {
        finish_inspector_transform_gesture(state, false);
    }
    return true;
}

} // namespace

void finalize_orphaned_inspector_transform_gesture(ShellState* state) {
    if (state == nullptr || !state->inspector_transform_gesture.has_value()) {
        return;
    }
    const ImGuiID item_id = state->inspector_transform_gesture->item_id;
    const ImGuiContext* context = ImGui::GetCurrentContext();
    const bool item_is_live = context != nullptr &&
        ImGui::GetActiveID() == item_id && context->ActiveIdIsAlive == item_id;
    if (item_is_live) {
        return;
    }
    const bool cancel = context != nullptr &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    finish_inspector_transform_gesture(state, !cancel);
}


void draw_inspector_window(ShellState* state) {
    ImGui::Begin(kPropertiesWindowTitle);
    widgets::panel_head(state->icons, Icon::PropTranslate, "Properties");

    if (!state->load_result || !state->preview_skeleton) {
        ImGui::TextUnformatted("Load a valid project to inspect setup-pose data.");
        ImGui::End();
        return;
    }

    // A live transform edit may replace the session's runtime data while this
    // window is being drawn. Keep the frame-start data alive until the panel
    // completes so labels and hierarchy references remain valid.
    const auto skeleton_data = state->load_result.skeleton_data;
    const auto& skeleton = *skeleton_data;
    const ResolvedSelection resolved = resolve_shell_selection(*state);
    const auto children = build_bone_children(skeleton);

    // Persistent context strip — what is currently being inspected.
    {
        namespace t = marrow::editor::shell::theme;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, t::kSurfaceCard);
        ImGui::BeginChild("props_ctx", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AutoResizeY);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (resolved.context_bone_index.has_value() &&
            *resolved.context_bone_index < skeleton.bones().size()) {
            widgets::context_line(
                state->icons, Icon::NodeBone, t::kPrimary,
                skeleton.bones()[*resolved.context_bone_index].name.c_str(),
                resolved.active_bone_index.has_value() ? "bone" : "owning bone");
        } else {
            ImGui::TextColored(t::kFaint, "No bone selected");
        }
        if (resolved.active_slot_index.has_value() &&
            *resolved.active_slot_index < skeleton.slots().size()) {
            widgets::context_line(
                state->icons, Icon::NodeSlot, t::kTertiaryDim,
                skeleton.slots()[*resolved.active_slot_index].name.c_str(),
                "slot");
        }
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    if (ImGui::CollapsingHeader("Bones", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("inspector_bones", ImVec2(0.0f, 140.0f), true);
        for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
            const auto& bone = skeleton.bones()[bone_index];
            const bool selected = resolved.active_bone_index == bone_index;
            std::string label = bone.name;
            if (bone.parent_index.has_value() && *bone.parent_index < skeleton.bones().size()) {
                label += " <- " + skeleton.bones()[*bone.parent_index].name;
            }
            ImGui::PushID(static_cast<int>(bone_index));
            if (icon_selectable(state->icons, Icon::NodeBone, label.c_str(), selected)) {
                select_bone(state, bone_index, "Inspector", true);
            }
            ImGui::PopID();
        }
        ImGui::EndChild();

        if (resolved.active_bone_index.has_value() &&
            *resolved.active_bone_index < skeleton.bones().size() &&
            *resolved.active_bone_index <
                state->preview_skeleton->bone_world_transforms().size()) {
            const std::size_t bone_index = *resolved.active_bone_index;
            const auto& bone = skeleton.bones()[bone_index];
            const auto& setup_pose = bone.setup_pose;

            ImGui::Spacing();
            ImGui::Text("Selected: %s", bone.name.c_str());
            ImGui::Text("Parent: %s", parent_bone_name(skeleton, bone).c_str());
            ImGui::Text("Children: %zu", children[bone_index].size());
            ImGui::Text("Slots: %s", join_slots_for_bone(skeleton, bone_index).c_str());
            ImGui::Text(
                "Active in preview: %s",
                yes_no(state->preview_skeleton->is_bone_active(bone_index)));
            ImGui::Separator();
            ImGui::TextUnformatted("Setup Pose");
            {
                char buf[128];
                std::snprintf(
                    buf, sizeof(buf),
                    "Translate: (%.1f, %.1f)",
                    static_cast<double>(setup_pose.x),
                    static_cast<double>(setup_pose.y));
                icon_label(state->icons, Icon::PropTranslate, buf, 0.75f);

                std::snprintf(
                    buf, sizeof(buf),
                    "Rotation: %.1f deg",
                    static_cast<double>(setup_pose.rotation));
                icon_label(state->icons, Icon::PropRotate, buf, 0.75f);

                std::snprintf(
                    buf, sizeof(buf),
                    "Scale: (%.2f, %.2f)",
                    static_cast<double>(setup_pose.scale_x),
                    static_cast<double>(setup_pose.scale_y));
                icon_label(state->icons, Icon::PropScale, buf, 0.75f);

                std::snprintf(
                    buf, sizeof(buf),
                    "Shear: (%.1f, %.1f)",
                    static_cast<double>(setup_pose.shear_x),
                    static_cast<double>(setup_pose.shear_y));
                icon_label(state->icons, Icon::PropShear, buf, 0.75f);
            }
            const char* inherit_label = "normal";
            switch (bone.inherit) {
            case marrow::runtime::BoneInherit::Normal:
                inherit_label = "normal";
                break;
            case marrow::runtime::BoneInherit::OnlyTranslation:
                inherit_label = "onlyTranslation";
                break;
            case marrow::runtime::BoneInherit::NoRotationOrReflection:
                inherit_label = "noRotationOrReflection";
                break;
            case marrow::runtime::BoneInherit::NoScale:
                inherit_label = "noScale";
                break;
            case marrow::runtime::BoneInherit::NoScaleOrReflection:
                inherit_label = "noScaleOrReflection";
                break;
            }
            ImGui::Text("Inherit: %s", inherit_label);
            ImGui::Separator();
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg, ImVec4(0.098f, 0.110f, 0.133f, 0.50f));
            ImGui::BeginChild(
                "local_pose_editor", ImVec2(0, 0),
                ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
            ImGui::TextUnformatted("Local Pose");
            ImGui::SameLine();
            const bool pose_editable = inspector_bone_pose_editable(*state);
            ImGui::TextDisabled(
                pose_editable
                    ? "(auto-key at playhead)"
                    : "(read-only; switch to Animation mode to key)");
            const marrow::runtime::BoneTransform local_pose =
                state->preview_skeleton->bone_poses()[bone_index].local_pose;
            if (pose_editable) {

                // Ghost input: transparent FrameBg + bottom underline that
                // illuminates to primary-container when focused.
                ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                    ImVec4(0.212f, 0.224f, 0.251f, 0.40f));
                ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                    ImVec4(0.376f, 0.545f, 1.000f, 0.20f));

                auto ghost_underline = [](bool active) {
                    const ImVec2 rmin = ImGui::GetItemRectMin();
                    const ImVec2 rmax = ImGui::GetItemRectMax();
                    const ImU32 color = active
                        ? IM_COL32(0x60, 0x8b, 0xff, 0xFF)   // primary-container
                        : IM_COL32(0x27, 0x2a, 0x30, 0xFF);  // surface-high
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(rmin.x, rmax.y - 1.0f),
                        ImVec2(rmax.x, rmax.y - 1.0f),
                        color,
                        2.0f);
                };

                float translate[2] = {local_pose.x, local_pose.y};
                const bool translate_changed = ImGui::DragFloat2(
                        "Translate##bone_local",
                        translate,
                        1.0f,
                        0.0f,
                        0.0f,
                        "%.1f");
                apply_inspector_transform_drag(
                    state,
                    translate_changed,
                    bone_index,
                    marrow::editor::TransformTimelineChannel::Translate,
                    marrow::editor::TransformKeyframePatch{
                        std::nullopt,
                        static_cast<double>(translate[0]),
                        static_cast<double>(translate[1])});
                ghost_underline(ImGui::IsItemActive() || ImGui::IsItemFocused());

                float rotation = local_pose.rotation;
                const bool rotation_changed = ImGui::DragFloat(
                        "Rotation##bone_local",
                        &rotation,
                        0.5f,
                        -360.0f,
                        360.0f,
                        "%.1f deg");
                apply_inspector_transform_drag(
                    state,
                    rotation_changed,
                    bone_index,
                    marrow::editor::TransformTimelineChannel::Rotate,
                    marrow::editor::TransformKeyframePatch{
                        static_cast<double>(rotation),
                        std::nullopt,
                        std::nullopt});
                ghost_underline(ImGui::IsItemActive() || ImGui::IsItemFocused());

                float scale[2] = {local_pose.scale_x, local_pose.scale_y};
                const bool scale_changed = ImGui::DragFloat2(
                        "Scale##bone_local",
                        scale,
                        0.01f,
                        0.0f,
                        0.0f,
                        "%.3f");
                apply_inspector_transform_drag(
                    state,
                    scale_changed,
                    bone_index,
                    marrow::editor::TransformTimelineChannel::Scale,
                    marrow::editor::TransformKeyframePatch{
                        std::nullopt,
                        static_cast<double>(scale[0]),
                        static_cast<double>(scale[1])});
                ghost_underline(ImGui::IsItemActive() || ImGui::IsItemFocused());

                float shear[2] = {local_pose.shear_x, local_pose.shear_y};
                const bool shear_changed = ImGui::DragFloat2(
                        "Shear##bone_local",
                        shear,
                        0.5f,
                        0.0f,
                        0.0f,
                        "%.1f");
                apply_inspector_transform_drag(
                    state,
                    shear_changed,
                    bone_index,
                    marrow::editor::TransformTimelineChannel::Shear,
                    marrow::editor::TransformKeyframePatch{
                        std::nullopt,
                        static_cast<double>(shear[0]),
                        static_cast<double>(shear[1])});
                ghost_underline(ImGui::IsItemActive() || ImGui::IsItemFocused());

                ImGui::PopStyleColor(3);
            } else {
                char pose_buffer[160];
                std::snprintf(
                    pose_buffer,
                    sizeof(pose_buffer),
                    "Translate: (%.1f, %.1f)",
                    static_cast<double>(local_pose.x),
                    static_cast<double>(local_pose.y));
                icon_label(state->icons, Icon::PropTranslate, pose_buffer, 0.75f);
                std::snprintf(
                    pose_buffer,
                    sizeof(pose_buffer),
                    "Rotation: %.1f deg",
                    static_cast<double>(local_pose.rotation));
                icon_label(state->icons, Icon::PropRotate, pose_buffer, 0.75f);
                std::snprintf(
                    pose_buffer,
                    sizeof(pose_buffer),
                    "Scale: (%.3f, %.3f)",
                    static_cast<double>(local_pose.scale_x),
                    static_cast<double>(local_pose.scale_y));
                icon_label(state->icons, Icon::PropScale, pose_buffer, 0.75f);
                std::snprintf(
                    pose_buffer,
                    sizeof(pose_buffer),
                    "Shear: (%.1f, %.1f)",
                    static_cast<double>(local_pose.shear_x),
                    static_cast<double>(local_pose.shear_y));
                icon_label(state->icons, Icon::PropShear, pose_buffer, 0.75f);
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::TextUnformatted("World Pose");
            const auto& current_world_transforms =
                state->preview_skeleton->bone_world_transforms();
            const marrow::runtime::BoneWorldTransform world =
                current_world_transforms[bone_index];
            ImGui::Text(
                "World position: (%.1f, %.1f)",
                static_cast<double>(world.world_x),
                static_cast<double>(world.world_y));
            ImGui::Text(
                "Basis X: (%.2f, %.2f)  Basis Y: (%.2f, %.2f)",
                static_cast<double>(world.a),
                static_cast<double>(world.c),
                static_cast<double>(world.b),
                static_cast<double>(world.d));
        } else {
            ImGui::Spacing();
            ImGui::TextUnformatted("Select a bone from the hierarchy or viewport.");
        }
    }

    if (ImGui::CollapsingHeader("Slots", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("inspector_slots", ImVec2(0.0f, 130.0f), true);
        for (std::size_t slot_index = 0; slot_index < skeleton.slots().size(); ++slot_index) {
            const auto& slot = skeleton.slots()[slot_index];
            const auto* current_attachment = state->preview_skeleton->current_attachment(slot_index);
            const bool selected = resolved.active_slot_index == slot_index;
            std::string label = slot.name + " -> " +
                (current_attachment != nullptr ? current_attachment->name : std::string("<none>"));
            if (ImGui::Selectable(
                    (label + "##inspector_slot_" + std::to_string(slot_index)).c_str(),
                    selected)) {
                select_slot(state, slot_index, "Inspector", true);
            }
        }
        ImGui::EndChild();

        if (resolved.active_slot_index.has_value() &&
            *resolved.active_slot_index < skeleton.slots().size() &&
            *resolved.active_slot_index < state->preview_skeleton->slot_states().size()) {
            const std::size_t slot_index = *resolved.active_slot_index;
            const auto& slot = skeleton.slots()[slot_index];
            const auto& slot_state = state->preview_skeleton->slot_states()[slot_index];
            const auto* current_attachment = state->preview_skeleton->current_attachment(slot_index);
            const auto current_selection = current_attachment_selection(*state, slot_index);
            const auto skin_preview_attachment = resolve_skin_preview_attachment(
                skeleton,
                state->preview_skin_names,
                slot_index);
            bool has_preview_override =
                current_selection.has_value() != skin_preview_attachment.has_value();
            if (!has_preview_override && current_selection.has_value()) {
                has_preview_override =
                    current_selection->attachment_name != skin_preview_attachment->attachment_name ||
                    current_selection->skin_index != skin_preview_attachment->skin_index;
            }

            const std::string source_skin = current_selection.has_value()
                ? source_skin_name(skeleton, current_selection->skin_index)
                : std::string("<none>");

            ImGui::Spacing();
            ImGui::Text("Selected slot: %s", slot.name.c_str());
            ImGui::Text("Bone: %s", skeleton.bones()[slot.bone_index].name.c_str());
            if (const auto order = draw_order_position(*state->preview_skeleton, slot_index)) {
                ImGui::Text(
                    "Draw order: %zu / %zu",
                    *order + 1U,
                    state->preview_skeleton->draw_order().size());
            }
            ImGui::Text("Blend mode: %s", blend_mode_name(slot.blend_mode));
            ImGui::Text(
                "Setup attachment: %s",
                slot.setup_attachment.empty() ? "<none>" : slot.setup_attachment.c_str());
            ImGui::Text(
                "Preview attachment: %s",
                current_attachment != nullptr ? current_attachment->name.c_str() : "<none>");
            ImGui::Text("Attachment source skin: %s", source_skin.c_str());
            ImGui::Text("Preview override: %s", yes_no(has_preview_override));
            ImGui::Text(
                "Light color: %s",
                format_slot_color(slot_state.color).c_str());
            if (slot_state.dark_color.has_value()) {
                ImGui::Text(
                    "Dark tint: %s",
                    format_slot_color(*slot_state.dark_color).c_str());
            } else {
                ImGui::Text("Dark tint: <none>");
            }
            ImGui::TextDisabled(
                "Slot colors are read-only until keyed color authoring is enabled.");
        } else {
            ImGui::Spacing();
            ImGui::TextUnformatted("Select a slot to inspect presentation state.");
        }
    }

    if (ImGui::CollapsingHeader("Attachments", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (resolved.active_slot_index.has_value() &&
            *resolved.active_slot_index < skeleton.slots().size()) {
            const std::size_t slot_index = *resolved.active_slot_index;
            const auto attachments = collect_slot_attachments(skeleton, slot_index);

            ImGui::BeginChild("inspector_attachments", ImVec2(0.0f, 130.0f), true);
            if (attachments.empty()) {
                ImGui::TextUnformatted("No attachments are available for this slot.");
            } else {
                for (const auto& attachment_reference : attachments) {
                    bool preview_active = false;
                    if (const auto preview_selection =
                            current_attachment_selection(*state, slot_index)) {
                        preview_active =
                            attachment_matches_selection(*preview_selection, attachment_reference);
                    }

                    const bool selected = resolved.active_attachment.has_value() &&
                        attachment_matches_selection(
                            *resolved.active_attachment,
                            attachment_reference);
                    std::string label = attachment_reference.attachment->name +
                        " [" + source_skin_name(skeleton, attachment_reference.skin_index) +
                        "] (" + attachment_kind_name(attachment_reference.attachment->kind) + ")";
                    if (preview_active) {
                        label += " [preview]";
                    }
                    if (ImGui::Selectable(
                            (label + "##inspector_attachment_" +
                             std::to_string(slot_index) + "_" +
                             std::to_string(attachment_reference.skin_index.value_or(0)) + "_" +
                             attachment_reference.attachment->name)
                                .c_str(),
                            selected)) {
                        select_attachment(
                            state,
                            PreviewAttachmentSelection{
                                slot_index,
                                attachment_reference.skin_index,
                                attachment_reference.attachment->name},
                            "Inspector",
                            true);
                    }
                }
            }
            ImGui::EndChild();

            const auto attachment_reference =
                resolved.active_attachment.has_value() &&
                    resolved.active_attachment->slot_index == slot_index
                ? resolve_attachment_reference(skeleton, *resolved.active_attachment)
                : std::nullopt;

            if (attachment_reference.has_value()) {
                if (ImGui::Button("Apply To Preview Slot")) {
                    apply_attachment_selection_to_preview_slot(
                        state,
                        *resolved.active_attachment,
                        "Inspector",
                        true);
                }
            }
            if (resolved.active_slot_index.has_value()) {
                if (attachment_reference.has_value()) {
                    ImGui::SameLine();
                }
                if (ImGui::Button("Reset Slot To Skin Preview")) {
                    reset_preview_slot_to_skin_selection(
                        state,
                        slot_index,
                        "Inspector",
                        true);
                }
            }

            if (attachment_reference.has_value()) {
                ImGui::Separator();
                draw_attachment_details(*state, *attachment_reference);
            } else if (!attachments.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Select an attachment to inspect its data.");
            }
        } else {
            ImGui::TextUnformatted("Select a slot to inspect its attachments.");
        }
    }

    if (ImGui::CollapsingHeader("Skin Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text(
            "Active composition: %s",
            preview_skin_summary(skeleton, state->preview_skin_names).c_str());
        if (const auto default_name = default_skin_name(skeleton)) {
            ImGui::BulletText("%s (base skin)", std::string(*default_name).c_str());
        }

        bool has_toggleable_skin = false;
        for (std::size_t skin_index = 0; skin_index < skeleton.skins().size(); ++skin_index) {
            const auto& skin = skeleton.skins()[skin_index];
            if (is_default_skin_index(skeleton, skin_index)) {
                continue;
            }

            has_toggleable_skin = true;
            bool enabled =
                std::find(
                    state->preview_skin_names.begin(),
                    state->preview_skin_names.end(),
                    skin.name) != state->preview_skin_names.end();
            if (ImGui::Checkbox((skin.name + "##preview_skin").c_str(), &enabled)) {
                set_preview_skin_enabled(state, skin_index, enabled, true);
            }

            std::size_t linked_mesh_count = 0;
            for (const auto& slot_attachment : skin.slot_attachments) {
                if (slot_attachment.attachment.kind == marrow::runtime::AttachmentKind::LinkedMesh) {
                    ++linked_mesh_count;
                }
            }

            ImGui::SameLine();
            ImGui::TextDisabled(
                "%zu slot attachments, %zu linked meshes",
                skin.slot_attachments.size(),
                linked_mesh_count);
        }

        if (!has_toggleable_skin) {
            ImGui::TextUnformatted("No additional skins are available for preview.");
        }
    }

    draw_viewport_settings(state);

    ImGui::End();
}

// Agent surface — optional, closed by default (Ctrl+L / toolbar toggle).
// The socket can be switched on/off here at runtime; the live op feed,
// ops/sec and attribution highlights remain a later step (actual agent
// control happens from Claude Code, not from this panel).

} // namespace marrow::editor::shell
