#include "skeleton_internal.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace marrow::runtime {
namespace {

std::string slot_context(const SkeletonData& data, std::size_t slot_index) {
    std::ostringstream stream;
    stream << "slot[" << slot_index << "]";
    if (slot_index < data.slots().size()) {
        stream << " '" << data.slots()[slot_index].name << "'";
    }
    return stream.str();
}

} // namespace

const AttachmentData* SkinData::find_attachment(std::size_t slot_index) const {
    const auto it = std::find_if(
        slot_attachments.begin(),
        slot_attachments.end(),
        [&](const SkinSlotData& slot_attachment) {
            return slot_attachment.slot_index == slot_index;
        });
    if (it == slot_attachments.end()) {
        return nullptr;
    }

    return &it->attachment;
}

const AttachmentData* SkinData::find_attachment(
    std::size_t slot_index,
    std::string_view attachment_name) const {
    const auto it = std::find_if(
        slot_attachments.begin(),
        slot_attachments.end(),
        [&](const SkinSlotData& slot_attachment) {
            return slot_attachment.slot_index == slot_index &&
                slot_attachment.attachment.name == attachment_name;
        });
    if (it == slot_attachments.end()) {
        return nullptr;
    }

    return &it->attachment;
}

const AttachmentData* SkinData::find_attachment(std::string_view attachment_name) const {
    const auto it = std::find_if(
        slot_attachments.begin(),
        slot_attachments.end(),
        [&](const SkinSlotData& slot_attachment) {
            return slot_attachment.attachment.name == attachment_name;
        });
    if (it == slot_attachments.end()) {
        return nullptr;
    }

    return &it->attachment;
}

void Skeleton::apply_setup_attachments() {
    if (slot_states_.size() != data_->slots().size()) {
        slot_states_.resize(data_->slots().size());
    }
    if (mesh_deform_states_.size() != data_->slots().size()) {
        mesh_deform_states_.resize(data_->slots().size());
    }

    for (std::size_t index = 0; index < data_->slots().size(); ++index) {
        slot_states_[index].attachment_name = data_->slots()[index].setup_attachment;
        slot_states_[index].color = data_->slots()[index].color;
        slot_states_[index].dark_color = data_->slots()[index].dark_color;
        mesh_deform_states_[index].attachment_name.clear();
        mesh_deform_states_[index].vertex_offsets.clear();
        std::optional<std::size_t> attachment_skin_index;
        data_->find_attachment_source(
            index,
            slot_states_[index].attachment_name,
            &attachment_skin_index);
        slot_states_[index].attachment_skin_index = attachment_skin_index;
    }

    apply_active_skin_attachments();
}

void Skeleton::apply_active_skin_attachments() {
    for (const std::size_t skin_index : active_skin_indices_) {
        if (skin_index >= data_->skins().size()) {
            continue;
        }

        const SkinData& skin = data_->skins()[skin_index];
        for (std::size_t slot_index = 0; slot_index < slot_states_.size(); ++slot_index) {
            const AttachmentData* matching_attachment =
                skin.find_attachment(slot_index, slot_states_[slot_index].attachment_name);
            if (matching_attachment != nullptr) {
                slot_states_[slot_index].attachment_skin_index = skin_index;
                continue;
            }

            const AttachmentData* single_attachment = nullptr;
            for (const SkinSlotData& slot_attachment : skin.slot_attachments) {
                if (slot_attachment.slot_index != slot_index) {
                    continue;
                }
                if (single_attachment != nullptr) {
                    single_attachment = nullptr;
                    break;
                }

                single_attachment = &slot_attachment.attachment;
            }
            if (single_attachment == nullptr) {
                continue;
            }

            slot_states_[slot_index].attachment_name = single_attachment->name;
            slot_states_[slot_index].attachment_skin_index = skin_index;
        }
    }
}

void Skeleton::update_active_skin_scopes(const std::vector<std::size_t>& skin_indices) {
    active_skin_indices_.clear();
    if (const std::optional<std::size_t> default_skin_index = data_->default_skin_index();
        default_skin_index.has_value()) {
        active_skin_indices_.push_back(*default_skin_index);
    }
    for (const std::size_t skin_index : skin_indices) {
        if (std::find(active_skin_indices_.begin(), active_skin_indices_.end(), skin_index) ==
            active_skin_indices_.end()) {
            active_skin_indices_.push_back(skin_index);
        }
    }

    active_bones_.assign(data_->bones().size(), true);
    active_ik_constraints_.assign(data_->ik_constraints().size(), true);
    active_path_constraints_.assign(data_->path_constraints().size(), true);
    active_transform_constraints_.assign(data_->transform_constraints().size(), true);
    active_physics_constraints_.assign(data_->physics_constraints().size(), true);

    for (const SkinData& skin : data_->skins()) {
        for (const std::size_t bone_index : skin.bone_indices) {
            if (bone_index < active_bones_.size()) {
                active_bones_[bone_index] = false;
            }
        }
        for (const std::size_t constraint_index : skin.ik_constraint_indices) {
            if (constraint_index < active_ik_constraints_.size()) {
                active_ik_constraints_[constraint_index] = false;
            }
        }
        for (const std::size_t constraint_index : skin.path_constraint_indices) {
            if (constraint_index < active_path_constraints_.size()) {
                active_path_constraints_[constraint_index] = false;
            }
        }
        for (const std::size_t constraint_index : skin.transform_constraint_indices) {
            if (constraint_index < active_transform_constraints_.size()) {
                active_transform_constraints_[constraint_index] = false;
            }
        }
        for (const std::size_t constraint_index : skin.physics_constraint_indices) {
            if (constraint_index < active_physics_constraints_.size()) {
                active_physics_constraints_[constraint_index] = false;
            }
        }
    }

    for (const std::size_t skin_index : active_skin_indices_) {
        if (skin_index >= data_->skins().size()) {
            continue;
        }

        const SkinData& skin = data_->skins()[skin_index];
        for (const std::size_t bone_index : skin.bone_indices) {
            if (bone_index < active_bones_.size()) {
                active_bones_[bone_index] = true;
            }
        }
        for (const std::size_t constraint_index : skin.ik_constraint_indices) {
            if (constraint_index < active_ik_constraints_.size()) {
                active_ik_constraints_[constraint_index] = true;
            }
        }
        for (const std::size_t constraint_index : skin.path_constraint_indices) {
            if (constraint_index < active_path_constraints_.size()) {
                active_path_constraints_[constraint_index] = true;
            }
        }
        for (const std::size_t constraint_index : skin.transform_constraint_indices) {
            if (constraint_index < active_transform_constraints_.size()) {
                active_transform_constraints_[constraint_index] = true;
            }
        }
        for (const std::size_t constraint_index : skin.physics_constraint_indices) {
            if (constraint_index < active_physics_constraints_.size()) {
                active_physics_constraints_[constraint_index] = true;
            }
        }
    }
}

bool Skeleton::apply_slot_attachment_keyframe(
    std::size_t slot_index,
    const std::optional<std::string>& attachment_name) {
    if (slot_index >= slot_states_.size() || slot_index >= data_->slots().size()) {
        report_error(
            "slot[" + std::to_string(slot_index) +
            "] attachment operation targeted an invalid slot index");
        return false;
    }

    if (!attachment_name.has_value()) {
        slot_states_[slot_index].attachment_name.clear();
        slot_states_[slot_index].attachment_skin_index.reset();
        return true;
    }

    if (attachment_name->empty()) {
        report_error(
            slot_context(*data_, slot_index) +
            " attachment operation rejected an empty attachment name");
        return false;
    }

    for (auto skin_it = active_skin_indices_.rbegin();
         skin_it != active_skin_indices_.rend();
         ++skin_it) {
        const AttachmentData* attachment =
            data_->find_attachment(*skin_it, slot_index, *attachment_name);
        if (attachment == nullptr) {
            continue;
        }

        slot_states_[slot_index].attachment_name = attachment->name;
        slot_states_[slot_index].attachment_skin_index = *skin_it;
        return true;
    }

    std::optional<std::size_t> attachment_skin_index;
    const AttachmentData* attachment =
        data_->find_attachment_source(slot_index, *attachment_name, &attachment_skin_index);
    if (attachment == nullptr) {
        report_error(
            slot_context(*data_, slot_index) +
            " attachment '" + *attachment_name + "' could not be resolved in any skin");
        return false;
    }

    slot_states_[slot_index].attachment_name = attachment->name;
    slot_states_[slot_index].attachment_skin_index = attachment_skin_index;
    return true;
}

bool Skeleton::apply_skin_indices(const std::vector<std::size_t>& skin_indices) {
    for (const std::size_t skin_index : skin_indices) {
        if (skin_index >= data_->skins().size()) {
            return false;
        }
    }

    update_active_skin_scopes(skin_indices);
    apply_setup_attachments();
    reset_update_throttle_state();
    return true;
}

bool Skeleton::set_skin(std::string_view skin_name) {
    const auto skin_index = data_->find_skin_index(skin_name);
    if (!skin_index.has_value()) {
        return false;
    }

    return apply_skin_indices({*skin_index});
}

bool Skeleton::set_skin_composition(const std::vector<std::string_view>& skin_names) {
    std::vector<std::size_t> skin_indices;
    skin_indices.reserve(skin_names.size());
    for (const std::string_view skin_name : skin_names) {
        const auto skin_index = data_->find_skin_index(skin_name);
        if (!skin_index.has_value()) {
            return false;
        }

        skin_indices.push_back(*skin_index);
    }

    return apply_skin_indices(skin_indices);
}

bool Skeleton::is_bone_active(std::size_t bone_index) const {
    if (bone_index >= active_bones_.size()) {
        return false;
    }

    return active_bones_[bone_index];
}

const AttachmentData* Skeleton::current_attachment(std::size_t slot_index) const {
    return resolve_current_attachment(slot_index, true);
}

const AttachmentData* Skeleton::resolve_current_attachment(
    std::size_t slot_index,
    bool report_errors) const {
    if (slot_index >= slot_states_.size()) {
        if (report_errors) {
            report_error(
                "slot[" + std::to_string(slot_index) +
                "] current attachment lookup targeted an invalid slot index");
        }
        return nullptr;
    }

    const SlotState& slot_state = slot_states_[slot_index];
    if (slot_state.attachment_name.empty()) {
        return nullptr;
    }

    if (slot_state.attachment_skin_index.has_value()) {
        const AttachmentData* attachment =
            data_->find_attachment(
                *slot_state.attachment_skin_index,
                slot_index,
                slot_state.attachment_name);
        if (attachment != nullptr) {
            return attachment;
        }
    }

    const AttachmentData* attachment =
        data_->find_attachment_source(slot_index, slot_state.attachment_name);
    if (attachment != nullptr) {
        return attachment;
    }

    if (report_errors) {
        std::string message =
            slot_context(*data_, slot_index) +
            " attachment '" + slot_state.attachment_name + "'";
        if (slot_state.attachment_skin_index.has_value()) {
            message +=
                " could not be resolved from recorded skin index " +
                std::to_string(*slot_state.attachment_skin_index);
        } else {
            message += " could not be resolved in any skin";
        }
        report_error(std::move(message));
    }

    return nullptr;
}

std::optional<std::size_t> Skeleton::current_sequence_frame(std::size_t slot_index) const {
    const AttachmentData* attachment = resolve_current_attachment(slot_index, true);
    if (attachment == nullptr || !attachment->sequence.has_value()) {
        return std::nullopt;
    }

    return sample_sequence_frame(*attachment->sequence, attachment_playback_time_);
}

std::string_view Skeleton::current_region_name(std::size_t slot_index) const {
    const AttachmentData* attachment = resolve_current_attachment(slot_index, false);
    if (attachment != nullptr) {
        if (attachment->sequence.has_value()) {
            const std::optional<std::size_t> sequence_frame = current_sequence_frame(slot_index);
            if (sequence_frame.has_value() &&
                *sequence_frame < attachment->sequence->frame_regions.size()) {
                return attachment->sequence->frame_regions[*sequence_frame];
            }
        }

        if (!attachment->region_name.empty()) {
            return attachment->region_name;
        }
    }

    if (slot_index < slot_states_.size()) {
        return slot_states_[slot_index].attachment_name;
    }

    return {};
}

const std::vector<double>* Skeleton::current_mesh_vertex_offsets(std::size_t slot_index) const {
    if (slot_index >= mesh_deform_states_.size()) {
        return nullptr;
    }

    const AttachmentData* attachment = resolve_current_attachment(slot_index, true);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        return nullptr;
    }

    const MeshDeformState& deform_state = mesh_deform_states_[slot_index];
    if (deform_state.vertex_offsets.empty() ||
        !detail::attachment_matches_mesh_deform_source(*attachment, deform_state.attachment_name) ||
        deform_state.vertex_offsets.size() != attachment->mesh_geometry->vertices.size()) {
        return nullptr;
    }

    return &deform_state.vertex_offsets;
}

std::optional<PointAttachmentPose> Skeleton::evaluate_current_point_attachment(
    std::size_t slot_index) const {
    if (slot_index >= data_->slots().size()) {
        return std::nullopt;
    }

    const AttachmentData* attachment = resolve_current_attachment(slot_index, true);
    if (attachment == nullptr || !attachment->point_attachment.has_value()) {
        return std::nullopt;
    }

    const std::size_t bone_index = data_->slots()[slot_index].bone_index;
    if (bone_index >= bone_world_transforms_.size()) {
        return std::nullopt;
    }

    const PointAttachmentData& point_attachment = *attachment->point_attachment;
    PointAttachmentPose pose;
    pose.slot_index = slot_index;
    pose.attachment_name = attachment->name;
    pose.position = detail::transform_attachment_vertex(
        bone_world_transforms_[bone_index],
        point_attachment.local_position.x,
        point_attachment.local_position.y);
    pose.rotation = detail::transform_attachment_rotation(
        bone_world_transforms_[bone_index],
        point_attachment.rotation);
    return pose;
}

std::optional<BoundingBoxAttachmentPose> Skeleton::evaluate_current_bounding_box_attachment(
    std::size_t slot_index) const {
    if (slot_index >= data_->slots().size()) {
        return std::nullopt;
    }

    const AttachmentData* attachment = resolve_current_attachment(slot_index, true);
    if (attachment == nullptr || !attachment->bounding_box.has_value()) {
        return std::nullopt;
    }

    const std::size_t bone_index = data_->slots()[slot_index].bone_index;
    if (bone_index >= bone_world_transforms_.size()) {
        return std::nullopt;
    }

    BoundingBoxAttachmentPose pose;
    pose.slot_index = slot_index;
    pose.attachment_name = attachment->name;
    pose.polygon.reserve(attachment->bounding_box->polygon.size());

    for (const AttachmentVertex& vertex : attachment->bounding_box->polygon) {
        pose.polygon.push_back(detail::transform_attachment_vertex(
            bone_world_transforms_[bone_index],
            vertex.x,
            vertex.y));
    }

    return pose;
}

std::optional<ClippingAttachmentPose> Skeleton::evaluate_current_clipping_attachment(
    std::size_t slot_index) const {
    if (slot_index >= data_->slots().size()) {
        return std::nullopt;
    }

    const AttachmentData* attachment = resolve_current_attachment(slot_index, true);
    if (attachment == nullptr || !attachment->clipping_attachment.has_value()) {
        return std::nullopt;
    }

    const std::size_t bone_index = data_->slots()[slot_index].bone_index;
    if (bone_index >= bone_world_transforms_.size()) {
        return std::nullopt;
    }

    ClippingAttachmentPose pose;
    pose.slot_index = slot_index;
    pose.attachment_name = attachment->name;
    pose.end_slot_index = attachment->clipping_attachment->end_slot_index;
    pose.end_slot_name = attachment->clipping_attachment->end_slot_name;
    pose.polygon.reserve(attachment->clipping_attachment->polygon.size());

    for (const AttachmentVertex& vertex : attachment->clipping_attachment->polygon) {
        pose.polygon.push_back(detail::transform_attachment_vertex(
            bone_world_transforms_[bone_index],
            vertex.x,
            vertex.y));
    }

    return pose;
}

std::optional<MeshAttachmentPose> Skeleton::evaluate_current_mesh_attachment(
    std::size_t slot_index) const {
    const AttachmentData* attachment = resolve_current_attachment(slot_index, true);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }

    const MeshGeometry& geometry = *attachment->mesh_geometry;
    const std::size_t vertex_count = geometry.vertices.size() / 2;
    if (vertex_count == 0 || geometry.weights.size() != vertex_count) {
        return std::nullopt;
    }

    MeshAttachmentPose pose;
    pose.slot_index = slot_index;
    pose.attachment_name = attachment->name;
    pose.region_name = std::string(current_region_name(slot_index));
    pose.vertices.reserve(vertex_count);
    pose.triangles = geometry.triangles;
    pose.uvs = geometry.uvs;

    const std::vector<double>* vertex_offsets = current_mesh_vertex_offsets(slot_index);

    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        const MeshGeometry::VertexWeights& vertex_weights = geometry.weights[vertex_index];
        const double offset_x = vertex_offsets != nullptr ? (*vertex_offsets)[vertex_index * 2] : 0.0;
        const double offset_y =
            vertex_offsets != nullptr ? (*vertex_offsets)[(vertex_index * 2) + 1] : 0.0;
        MeshWorldVertex world_vertex;
        for (const MeshGeometry::VertexWeight& influence : vertex_weights.influences) {
            if (influence.bone_index >= bone_world_transforms_.size()) {
                report_error(
                    slot_context(*data_, slot_index) +
                    " mesh attachment '" + attachment->name +
                    "' references invalid bone index " +
                    std::to_string(influence.bone_index) +
                    " while evaluating weighted vertices (bone palette=" +
                    std::to_string(bone_world_transforms_.size()) + ")");
                return std::nullopt;
            }

            const MeshWorldVertex transformed = detail::transform_mesh_point(
                bone_world_transforms_[influence.bone_index],
                influence.x + offset_x,
                influence.y + offset_y);
            world_vertex.x += transformed.x * influence.weight;
            world_vertex.y += transformed.y * influence.weight;
        }

        pose.vertices.push_back(world_vertex);
    }

    return pose;
}

void Skeleton::report_error(std::string message) const {
    last_error_ = std::move(message);
    if (error_callback_) {
        error_callback_(*last_error_);
    }
}

} // namespace marrow::runtime
