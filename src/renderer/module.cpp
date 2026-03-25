#include "marrow/renderer/module.hpp"

#include <array>
#include <sstream>
#include <utility>

namespace marrow::renderer {

namespace {

RenderPoint transform_point(
    const runtime::BoneWorldTransform& transform,
    const RenderPoint& local_point) {
    return {
        transform.a * local_point.x + transform.b * local_point.y + transform.world_x,
        transform.c * local_point.x + transform.d * local_point.y + transform.world_y};
}

RenderPoint normalized_uv(double x, double y, const runtime::AtlasData& atlas) {
    return {x / atlas.info().width, y / atlas.info().height};
}

std::string slot_error(std::size_t slot_index, std::string_view message) {
    std::ostringstream stream;
    stream << "slot[" << slot_index << "] " << message;
    return stream.str();
}

} // namespace

PreparedSceneResult prepare_setup_pose_scene(
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas) {
    PreparedSceneResult result;

    const auto& data = skeleton.data();
    const auto& slots = data->slots();
    const auto& slot_states = skeleton.slot_states();
    const auto& draw_order = skeleton.draw_order();
    const auto& bone_world_transforms = skeleton.bone_world_transforms();

    if (slot_states.size() != slots.size()) {
        result.error_message = "slot state count does not match SkeletonData slots";
        return result;
    }
    if (draw_order.size() != slots.size()) {
        result.error_message = "draw order count does not match SkeletonData slots";
        return result;
    }

    PreparedScene scene;
    scene.atlas_name = atlas.info().name;
    scene.atlas_image = atlas.info().image;
    scene.skeleton_name = data->info().name;
    scene.region_attachments.reserve(draw_order.size());

    for (std::size_t draw_index = 0; draw_index < draw_order.size(); ++draw_index) {
        const std::size_t slot_index = draw_order[draw_index];
        if (slot_index >= slots.size()) {
            result.error_message = slot_error(slot_index, "is outside the skeleton slot range");
            return result;
        }

        const runtime::SlotData& slot = slots[slot_index];
        if (slot.bone_index >= bone_world_transforms.size()) {
            result.error_message =
                slot_error(slot_index, "references a bone outside the world transform buffer");
            return result;
        }

        const std::string& attachment_name = slot_states[slot_index].attachment_name;
        if (attachment_name.empty()) {
            continue;
        }

        const runtime::AtlasRegion* region = atlas.find_region_for_attachment(attachment_name);
        if (region == nullptr) {
            result.error_message = slot_error(
                slot_index,
                "attachment '" + attachment_name + "' does not resolve to an atlas region");
            return result;
        }

        const runtime::BoneWorldTransform& bone_world = bone_world_transforms[slot.bone_index];
        const std::array<RenderPoint, 4> local_corners{{
            {-region->origin_x, -region->origin_y},
            {region->width - region->origin_x, -region->origin_y},
            {region->width - region->origin_x, region->height - region->origin_y},
            {-region->origin_x, region->height - region->origin_y},
        }};
        const std::array<RenderPoint, 4> uv_corners{{
            normalized_uv(region->x, region->y, atlas),
            normalized_uv(region->x + region->width, region->y, atlas),
            normalized_uv(region->x + region->width, region->y + region->height, atlas),
            normalized_uv(region->x, region->y + region->height, atlas),
        }};

        RegionAttachmentDrawCommand command;
        command.slot_name = slot.name;
        command.attachment_name = attachment_name;
        command.atlas_region_name = region->name;
        command.slot_index = slot_index;
        command.bone_index = slot.bone_index;

        for (std::size_t vertex_index = 0; vertex_index < command.vertices.size(); ++vertex_index) {
            command.vertices[vertex_index].position =
                transform_point(bone_world, local_corners[vertex_index]);
            command.vertices[vertex_index].uv = uv_corners[vertex_index];
        }

        scene.region_attachments.push_back(std::move(command));
    }

    result.scene = std::move(scene);
    return result;
}

DemoShell::DemoShell(SampleAppWindow window, PreparedScene scene)
    : window_(std::move(window)),
      scene_(std::move(scene)) {}

std::string DemoShell::launch_report() const {
    std::ostringstream stream;
    stream << validation_target_name() << " launching " << component_name() << '\n'
           << "window: " << window_.title << " (" << window_.width << "x"
           << window_.height << ")\n"
           << "prepared scene: skeleton=" << scene_.skeleton_name
           << ", atlas=" << scene_.atlas_name
           << ", image=" << scene_.atlas_image << '\n'
           << "region attachments: " << scene_.region_attachments.size();

    for (const RegionAttachmentDrawCommand& attachment : scene_.region_attachments) {
        const RegionAttachmentVertex& min_corner = attachment.vertices[0];
        const RegionAttachmentVertex& max_corner = attachment.vertices[2];
        stream << '\n'
               << "slot[" << attachment.slot_index << "] " << attachment.slot_name
               << " -> attachment=" << attachment.attachment_name
               << ", region=" << attachment.atlas_region_name
               << ", bone=" << attachment.bone_index
               << ", quadMin=(" << min_corner.position.x << ", " << min_corner.position.y << ")"
               << ", quadMax=(" << max_corner.position.x << ", " << max_corner.position.y << ")";
    }
    return stream.str();
}

std::string_view component_name() {
    return "marrow-renderer";
}

std::string_view validation_target_name() {
    return "marrow_renderer_sample";
}

} // namespace marrow::renderer
