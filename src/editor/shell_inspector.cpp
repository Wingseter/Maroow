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


void draw_inspector_window(ShellState* state) {
    ImGui::Begin(kPropertiesWindowTitle);
    widgets::panel_head(state->icons, Icon::PropTranslate, "Properties");

    if (!state->load_result || !state->preview_skeleton) {
        ImGui::TextUnformatted("Load a valid project to inspect setup-pose data.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const auto children = build_bone_children(skeleton);
    const auto& world_transforms = state->preview_skeleton->bone_world_transforms();

    // Persistent context strip — what is currently being inspected.
    {
        namespace t = marrow::editor::shell::theme;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, t::kSurfaceCard);
        ImGui::BeginChild("props_ctx", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AutoResizeY);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        if (state->selected_bone_index.has_value() &&
            *state->selected_bone_index < skeleton.bones().size()) {
            widgets::context_line(
                state->icons, Icon::NodeBone, t::kPrimary,
                skeleton.bones()[*state->selected_bone_index].name.c_str(),
                "bone");
        } else {
            ImGui::TextColored(t::kFaint, "No bone selected");
        }
        if (state->selected_slot_index.has_value() &&
            *state->selected_slot_index < skeleton.slots().size()) {
            widgets::context_line(
                state->icons, Icon::NodeSlot, t::kTertiaryDim,
                skeleton.slots()[*state->selected_slot_index].name.c_str(),
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
            const bool selected =
                state->selected_bone_index.has_value() && *state->selected_bone_index == bone_index;
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

        if (state->selected_bone_index.has_value() &&
            *state->selected_bone_index < skeleton.bones().size() &&
            *state->selected_bone_index < world_transforms.size()) {
            const std::size_t bone_index = *state->selected_bone_index;
            const auto& bone = skeleton.bones()[bone_index];
            const auto& world = world_transforms[bone_index];
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
            ImGui::TextDisabled("(editable)");
            {
                auto& local_pose =
                    state->preview_skeleton->bone_poses()[bone_index].local_pose;
                bool pose_changed = false;

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
                if (ImGui::DragFloat2(
                        "Translate##bone_local",
                        translate,
                        1.0f,
                        0.0f,
                        0.0f,
                        "%.1f")) {
                    local_pose.x = translate[0];
                    local_pose.y = translate[1];
                    pose_changed = true;
                }
                ghost_underline(ImGui::IsItemActive() || ImGui::IsItemFocused());

                float rotation = local_pose.rotation;
                if (ImGui::DragFloat(
                        "Rotation##bone_local",
                        &rotation,
                        0.5f,
                        -360.0f,
                        360.0f,
                        "%.1f deg")) {
                    local_pose.rotation = rotation;
                    pose_changed = true;
                }
                ghost_underline(ImGui::IsItemActive() || ImGui::IsItemFocused());

                float scale[2] = {local_pose.scale_x, local_pose.scale_y};
                if (ImGui::DragFloat2(
                        "Scale##bone_local",
                        scale,
                        0.01f,
                        0.0f,
                        0.0f,
                        "%.3f")) {
                    local_pose.scale_x = scale[0];
                    local_pose.scale_y = scale[1];
                    pose_changed = true;
                }
                ghost_underline(ImGui::IsItemActive() || ImGui::IsItemFocused());

                float shear[2] = {local_pose.shear_x, local_pose.shear_y};
                if (ImGui::DragFloat2(
                        "Shear##bone_local",
                        shear,
                        0.5f,
                        0.0f,
                        0.0f,
                        "%.1f")) {
                    local_pose.shear_x = shear[0];
                    local_pose.shear_y = shear[1];
                    pose_changed = true;
                }
                ghost_underline(ImGui::IsItemActive() || ImGui::IsItemFocused());

                ImGui::PopStyleColor(3);

                if (pose_changed) {
                    state->preview_skeleton->update_world_transforms();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::TextUnformatted("World Pose");
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
            const bool selected =
                state->selected_slot_index.has_value() && *state->selected_slot_index == slot_index;
            std::string label = slot.name + " -> " +
                (current_attachment != nullptr ? current_attachment->name : std::string("<none>"));
            if (ImGui::Selectable(
                    (label + "##inspector_slot_" + std::to_string(slot_index)).c_str(),
                    selected)) {
                select_slot(state, slot_index, "Inspector", true);
            }
        }
        ImGui::EndChild();

        if (state->selected_slot_index.has_value() &&
            *state->selected_slot_index < skeleton.slots().size() &&
            *state->selected_slot_index < state->preview_skeleton->slot_states().size()) {
            const std::size_t slot_index = *state->selected_slot_index;
            const auto& slot = skeleton.slots()[slot_index];
            auto& slot_state = state->preview_skeleton->slot_states()[slot_index];
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
            float light_color[4] = {
                slot_state.color.r,
                slot_state.color.g,
                slot_state.color.b,
                slot_state.color.a};
            if (ImGui::ColorEdit4(
                    "Light color##slot_color",
                    light_color,
                    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                slot_state.color.r = light_color[0];
                slot_state.color.g = light_color[1];
                slot_state.color.b = light_color[2];
                slot_state.color.a = light_color[3];
            }
            if (slot_state.dark_color.has_value()) {
                float dark[4] = {
                    slot_state.dark_color->r,
                    slot_state.dark_color->g,
                    slot_state.dark_color->b,
                    slot_state.dark_color->a};
                if (ImGui::ColorEdit4(
                        "Dark tint##slot_dark",
                        dark,
                        ImGuiColorEditFlags_AlphaBar |
                            ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    slot_state.dark_color->r = dark[0];
                    slot_state.dark_color->g = dark[1];
                    slot_state.dark_color->b = dark[2];
                    slot_state.dark_color->a = dark[3];
                }
            } else {
                ImGui::Text("Dark tint: <none>");
            }
        } else {
            ImGui::Spacing();
            ImGui::TextUnformatted("Select a slot to inspect presentation state.");
        }
    }

    if (ImGui::CollapsingHeader("Attachments", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state->selected_slot_index.has_value() &&
            *state->selected_slot_index < skeleton.slots().size()) {
            const std::size_t slot_index = *state->selected_slot_index;
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

                    const bool selected =
                        state->selected_attachment.has_value() &&
                        attachment_matches_selection(
                            *state->selected_attachment,
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
                            AttachmentSelection{
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
                state->selected_attachment.has_value() &&
                    state->selected_attachment->slot_index == slot_index
                ? resolve_attachment_reference(skeleton, *state->selected_attachment)
                : std::nullopt;

            if (attachment_reference.has_value()) {
                if (ImGui::Button("Apply To Preview Slot")) {
                    apply_attachment_selection_to_preview_slot(
                        state,
                        *state->selected_attachment,
                        "Inspector",
                        true);
                }
            }
            if (state->selected_slot_index.has_value()) {
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
