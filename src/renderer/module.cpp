#include "marrow/renderer/module.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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

RenderPoint mesh_uv(
    double u,
    double v,
    const runtime::AtlasRegion& region,
    const runtime::AtlasData& atlas) {
    return normalized_uv(
        region.x + (u * region.width),
        region.y + (v * region.height),
        atlas);
}

std::string slot_error(std::size_t slot_index, std::string_view message) {
    std::ostringstream stream;
    stream << "slot[" << slot_index << "] " << message;
    return stream.str();
}

std::string_view mesh_buffer_usage_name(MeshBufferUsage usage) {
    switch (usage) {
    case MeshBufferUsage::Static:
        return "static";
    case MeshBufferUsage::Dynamic:
        return "dynamic";
    }

    return "unknown";
}

std::string_view mesh_shader_path_name(MeshShaderPath shader_path) {
    switch (shader_path) {
    case MeshShaderPath::GpuSkinning:
        return "gpu_skinning";
    }

    return "unknown";
}

std::string_view blend_mode_name(runtime::BlendMode blend_mode) {
    switch (blend_mode) {
    case runtime::BlendMode::Normal:
        return "normal";
    case runtime::BlendMode::Additive:
        return "additive";
    case runtime::BlendMode::Multiply:
        return "multiply";
    case runtime::BlendMode::Screen:
        return "screen";
    }

    return "unknown";
}

struct ClipVertex {
    RenderPoint position;
    RenderPoint uv;
};

struct ActiveClipState {
    std::string attachment_name;
    std::optional<std::size_t> end_slot_index;
    std::vector<RenderPoint> polygon;
};

double polygon_signed_area(const std::vector<RenderPoint>& polygon) {
    if (polygon.size() < 3) {
        return 0.0;
    }

    double area = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const RenderPoint& current = polygon[index];
        const RenderPoint& next = polygon[(index + 1) % polygon.size()];
        area += (current.x * next.y) - (next.x * current.y);
    }

    return area * 0.5;
}

double edge_cross(
    const RenderPoint& edge_start,
    const RenderPoint& edge_end,
    const RenderPoint& point) {
    return ((edge_end.x - edge_start.x) * (point.y - edge_start.y)) -
        ((edge_end.y - edge_start.y) * (point.x - edge_start.x));
}

bool inside_clip_edge(
    const RenderPoint& edge_start,
    const RenderPoint& edge_end,
    const RenderPoint& point,
    double orientation_sign) {
    constexpr double kTolerance = 1e-9;
    const double cross = edge_cross(edge_start, edge_end, point);
    return orientation_sign >= 0.0 ? cross >= -kTolerance : cross <= kTolerance;
}

ClipVertex intersect_clip_edge(
    const ClipVertex& start,
    const ClipVertex& end,
    const RenderPoint& edge_start,
    const RenderPoint& edge_end) {
    const double dx = end.position.x - start.position.x;
    const double dy = end.position.y - start.position.y;
    const double edge_dx = edge_end.x - edge_start.x;
    const double edge_dy = edge_end.y - edge_start.y;
    const double denominator = (dx * edge_dy) - (dy * edge_dx);

    double t = 0.0;
    if (std::abs(denominator) > 1e-9) {
        const double start_to_edge_x = edge_start.x - start.position.x;
        const double start_to_edge_y = edge_start.y - start.position.y;
        t = ((start_to_edge_x * edge_dy) - (start_to_edge_y * edge_dx)) / denominator;
        t = std::clamp(t, 0.0, 1.0);
    }

    return {
        {
            start.position.x + (dx * t),
            start.position.y + (dy * t),
        },
        {
            start.uv.x + ((end.uv.x - start.uv.x) * t),
            start.uv.y + ((end.uv.y - start.uv.y) * t),
        }};
}

std::vector<ClipVertex> clip_polygon_against_convex_polygon(
    const std::vector<ClipVertex>& subject_polygon,
    const std::vector<RenderPoint>& clip_polygon) {
    if (subject_polygon.empty() || clip_polygon.size() < 3) {
        return {};
    }

    std::vector<ClipVertex> output = subject_polygon;
    const double orientation_sign = polygon_signed_area(clip_polygon);
    for (std::size_t edge_index = 0; edge_index < clip_polygon.size() && !output.empty();
         ++edge_index) {
        const RenderPoint& edge_start = clip_polygon[edge_index];
        const RenderPoint& edge_end = clip_polygon[(edge_index + 1) % clip_polygon.size()];
        std::vector<ClipVertex> input = std::move(output);
        output.clear();

        ClipVertex previous = input.back();
        bool previous_inside =
            inside_clip_edge(edge_start, edge_end, previous.position, orientation_sign);
        for (const ClipVertex& current : input) {
            const bool current_inside =
                inside_clip_edge(edge_start, edge_end, current.position, orientation_sign);
            if (current_inside != previous_inside) {
                output.push_back(intersect_clip_edge(previous, current, edge_start, edge_end));
            }
            if (current_inside) {
                output.push_back(current);
            }

            previous = current;
            previous_inside = current_inside;
        }
    }

    return output;
}

template <typename Vertex>
std::vector<std::size_t> triangle_fan_indices(const std::vector<Vertex>& vertices) {
    std::vector<std::size_t> indices;
    if (vertices.size() < 3) {
        return indices;
    }

    indices.reserve((vertices.size() - 2) * 3);
    for (std::size_t index = 1; index + 1 < vertices.size(); ++index) {
        indices.push_back(0);
        indices.push_back(index);
        indices.push_back(index + 1);
    }

    return indices;
}

std::vector<SkinnedMeshVertex> evaluate_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette) {
    std::vector<SkinnedMeshVertex> vertices;
    vertices.reserve(attachment.vertex_payloads.size());

    for (const GpuSkinningVertexPayload& payload : attachment.vertex_payloads) {
        SkinnedMeshVertex vertex;
        vertex.uv = payload.uv;

        for (std::size_t influence_index = 0; influence_index < payload.influence_count;
             ++influence_index) {
            const std::size_t bone_index = payload.bone_indices[influence_index];
            if (bone_index >= bone_palette.size()) {
                return {};
            }

            const RenderPoint transformed = transform_point(
                bone_palette[bone_index],
                payload.bone_local_positions[influence_index]);
            vertex.position.x += transformed.x * payload.bone_weights[influence_index];
            vertex.position.y += transformed.y * payload.bone_weights[influence_index];
        }

        vertices.push_back(std::move(vertex));
    }

    return vertices;
}

void apply_region_mask_geometry(
    RegionAttachmentDrawCommand* command,
    const std::optional<ActiveClipState>& active_clip) {
    command->masked_vertices.assign(command->vertices.begin(), command->vertices.end());
    command->masked_indices = {0, 1, 2, 0, 2, 3};

    if (!active_clip.has_value()) {
        return;
    }

    command->clip_attachment_name = active_clip->attachment_name;
    const std::vector<ClipVertex> subject_polygon{
        ClipVertex{command->vertices[0].position, command->vertices[0].uv},
        ClipVertex{command->vertices[1].position, command->vertices[1].uv},
        ClipVertex{command->vertices[2].position, command->vertices[2].uv},
        ClipVertex{command->vertices[3].position, command->vertices[3].uv},
    };
    const std::vector<ClipVertex> clipped =
        clip_polygon_against_convex_polygon(subject_polygon, active_clip->polygon);

    command->masked_vertices.clear();
    command->masked_vertices.reserve(clipped.size());
    for (const ClipVertex& vertex : clipped) {
        command->masked_vertices.push_back({vertex.position, vertex.uv});
    }
    command->masked_indices = triangle_fan_indices(command->masked_vertices);
}

void apply_dynamic_mesh_mask_geometry(
    DynamicMeshDrawCommand* command,
    const std::vector<runtime::BoneWorldTransform>& bone_palette,
    const std::optional<ActiveClipState>& active_clip) {
    command->masked_vertices = evaluate_skinned_vertices(*command, bone_palette);
    command->masked_indices = command->indices;

    if (!active_clip.has_value()) {
        return;
    }

    command->clip_attachment_name = active_clip->attachment_name;
    std::vector<SkinnedMeshVertex> clipped_vertices;
    std::vector<std::size_t> clipped_indices;
    for (std::size_t index = 0; index + 2 < command->indices.size(); index += 3) {
        if (command->indices[index] >= command->masked_vertices.size() ||
            command->indices[index + 1] >= command->masked_vertices.size() ||
            command->indices[index + 2] >= command->masked_vertices.size()) {
            continue;
        }

        const std::vector<ClipVertex> subject_triangle{
            ClipVertex{
                command->masked_vertices[command->indices[index]].position,
                command->masked_vertices[command->indices[index]].uv},
            ClipVertex{
                command->masked_vertices[command->indices[index + 1]].position,
                command->masked_vertices[command->indices[index + 1]].uv},
            ClipVertex{
                command->masked_vertices[command->indices[index + 2]].position,
                command->masked_vertices[command->indices[index + 2]].uv},
        };
        const std::vector<ClipVertex> clipped_triangle =
            clip_polygon_against_convex_polygon(subject_triangle, active_clip->polygon);
        if (clipped_triangle.size() < 3) {
            continue;
        }

        const std::size_t base_index = clipped_vertices.size();
        for (const ClipVertex& vertex : clipped_triangle) {
            clipped_vertices.push_back({vertex.position, vertex.uv});
        }
        const std::vector<std::size_t> fan = triangle_fan_indices(clipped_triangle);
        for (const std::size_t clipped_index : fan) {
            clipped_indices.push_back(base_index + clipped_index);
        }
    }

    command->masked_vertices = std::move(clipped_vertices);
    command->masked_indices = std::move(clipped_indices);
}

std::optional<std::string> append_dynamic_mesh_attachment(
    PreparedScene* scene,
    const runtime::AttachmentData& attachment,
    const runtime::SlotData& slot,
    const runtime::SlotState& slot_state,
    std::size_t slot_index,
    const std::vector<double>* vertex_offsets,
    const runtime::AtlasRegion& region,
    const runtime::AtlasData& atlas,
    const std::vector<runtime::BoneWorldTransform>& bone_palette,
    const std::optional<ActiveClipState>& active_clip) {
    if (attachment.mesh_geometry == nullptr) {
        return slot_error(slot_index, "mesh attachment is missing geometry");
    }

    const runtime::MeshGeometry& geometry = *attachment.mesh_geometry;
    if (geometry.vertices.size() % 2 != 0) {
        return slot_error(slot_index, "mesh vertices must contain x/y pairs");
    }

    const std::size_t vertex_count = geometry.vertices.size() / 2;
    if (vertex_count == 0) {
        return slot_error(slot_index, "mesh attachments require at least one vertex");
    }
    if (geometry.uvs.size() != geometry.vertices.size()) {
        return slot_error(slot_index, "mesh uv coordinates must match the vertex count");
    }
    if (geometry.weights.size() != vertex_count) {
        return slot_error(slot_index, "mesh weights must match the vertex count");
    }
    if (vertex_offsets != nullptr && vertex_offsets->size() != geometry.vertices.size()) {
        return slot_error(slot_index, "mesh deform offsets must align with the vertex count");
    }

    DynamicMeshDrawCommand command;
    command.slot_name = slot.name;
    command.attachment_name = attachment.name;
    command.atlas_region_name = region.name;
    command.slot_index = slot_index;
    command.blend_mode = slot.blend_mode;
    command.color = slot_state.color;
    command.dark_color = slot_state.dark_color;
    command.indices = geometry.triangles;
    command.vertex_payloads.reserve(vertex_count);

    for (const std::size_t triangle_index : command.indices) {
        if (triangle_index >= vertex_count) {
            return slot_error(slot_index, "mesh triangle indices exceed the uploaded vertex count");
        }
    }

    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        const runtime::MeshGeometry::VertexWeights& vertex_weights = geometry.weights[vertex_index];
        if (vertex_weights.influences.empty()) {
            return slot_error(slot_index, "mesh vertices must preserve at least one bone influence");
        }
        if (vertex_weights.influences.size() > kMaxGpuSkinningInfluences) {
            return slot_error(slot_index, "mesh vertices exceed the 4-bone GPU skinning limit");
        }

        GpuSkinningVertexPayload payload;
        payload.influence_count = vertex_weights.influences.size();
        payload.uv = mesh_uv(
            geometry.uvs[(vertex_index * 2) + 0],
            geometry.uvs[(vertex_index * 2) + 1],
            region,
            atlas);

        for (std::size_t influence_index = 0; influence_index < vertex_weights.influences.size();
             ++influence_index) {
            const runtime::MeshGeometry::VertexWeight& influence =
                vertex_weights.influences[influence_index];
            if (influence.bone_index >= bone_palette.size()) {
                return slot_error(slot_index, "mesh influence references a missing bone palette entry");
            }

            const double offset_x = vertex_offsets != nullptr ? (*vertex_offsets)[vertex_index * 2] : 0.0;
            const double offset_y =
                vertex_offsets != nullptr ? (*vertex_offsets)[(vertex_index * 2) + 1] : 0.0;
            payload.bone_local_positions[influence_index] = {
                influence.x + offset_x,
                influence.y + offset_y};
            payload.bone_indices[influence_index] = influence.bone_index;
            payload.bone_weights[influence_index] = influence.weight;
        }

        command.vertex_payloads.push_back(std::move(payload));
    }

    apply_dynamic_mesh_mask_geometry(&command, bone_palette, active_clip);

    scene->dynamic_mesh_attachments.push_back(std::move(command));
    return std::nullopt;
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
    scene.bone_palette = bone_world_transforms;
    scene.clip_attachments.reserve(draw_order.size());
    scene.region_attachments.reserve(draw_order.size());
    scene.dynamic_mesh_attachments.reserve(draw_order.size());
    std::optional<ActiveClipState> active_clip;

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

        const runtime::AttachmentData* attachment = skeleton.current_attachment(slot_index);
        const auto clear_clip_if_needed = [&]() {
            if (active_clip.has_value() && active_clip->end_slot_index.has_value() &&
                *active_clip->end_slot_index == slot_index) {
                active_clip.reset();
            }
        };

        if (attachment != nullptr && attachment->kind == runtime::AttachmentKind::Clipping) {
            const std::optional<runtime::ClippingAttachmentPose> clip_pose =
                skeleton.evaluate_current_clipping_attachment(slot_index);
            if (!clip_pose.has_value()) {
                result.error_message =
                    slot_error(slot_index, "clipping attachment is missing clipping polygon data");
                return result;
            }

            ClipAttachmentDrawCommand command;
            command.slot_name = slot.name;
            command.attachment_name = clip_pose->attachment_name;
            command.slot_index = slot_index;
            command.end_slot_index = clip_pose->end_slot_index;
            command.end_slot_name = clip_pose->end_slot_name;
            command.polygon.reserve(clip_pose->polygon.size());

            ActiveClipState clip_state;
            clip_state.attachment_name = clip_pose->attachment_name;
            clip_state.end_slot_index = clip_pose->end_slot_index;
            clip_state.polygon.reserve(clip_pose->polygon.size());

            for (const runtime::AttachmentVertex& vertex : clip_pose->polygon) {
                const RenderPoint point{vertex.x, vertex.y};
                command.polygon.push_back(point);
                clip_state.polygon.push_back(point);
            }

            scene.clip_attachments.push_back(std::move(command));
            active_clip = std::move(clip_state);
            continue;
        }

        if (attachment != nullptr &&
            (attachment->kind == runtime::AttachmentKind::Point ||
             attachment->kind == runtime::AttachmentKind::BoundingBox ||
             attachment->kind == runtime::AttachmentKind::Path)) {
            clear_clip_if_needed();
            continue;
        }

        const std::string_view region_name = skeleton.current_region_name(slot_index);
        const runtime::AtlasRegion* region = atlas.find_region_for_attachment(region_name);
        if (region == nullptr) {
            result.error_message = slot_error(
                slot_index,
                "attachment '" + std::string(region_name) + "' does not resolve to an atlas region");
            return result;
        }

        if (attachment != nullptr && attachment->mesh_geometry != nullptr) {
            const std::vector<double>* vertex_offsets =
                skeleton.current_mesh_vertex_offsets(slot_index);
            if (const std::optional<std::string> error = append_dynamic_mesh_attachment(
                    &scene,
                    *attachment,
                    slot,
                    slot_states[slot_index],
                    slot_index,
                    vertex_offsets,
                    *region,
                    atlas,
                    scene.bone_palette,
                    active_clip)) {
                result.error_message = *error;
                return result;
            }
            clear_clip_if_needed();
            continue;
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
        command.blend_mode = slot.blend_mode;
        command.color = slot_states[slot_index].color;
        command.dark_color = slot_states[slot_index].dark_color;

        for (std::size_t vertex_index = 0; vertex_index < command.vertices.size(); ++vertex_index) {
            command.vertices[vertex_index].position =
                transform_point(bone_world, local_corners[vertex_index]);
            command.vertices[vertex_index].uv = uv_corners[vertex_index];
        }

        apply_region_mask_geometry(&command, active_clip);
        scene.region_attachments.push_back(std::move(command));
        clear_clip_if_needed();
    }

    result.scene = std::move(scene);
    return result;
}

std::vector<SkinnedMeshVertex> evaluate_gpu_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette) {
    return evaluate_skinned_vertices(attachment, bone_palette);
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
           << "bone palette entries: " << scene_.bone_palette.size() << '\n'
           << "clip attachments: " << scene_.clip_attachments.size() << '\n'
           << "region attachments: " << scene_.region_attachments.size() << '\n'
           << "dynamic mesh attachments: " << scene_.dynamic_mesh_attachments.size();

    for (const ClipAttachmentDrawCommand& attachment : scene_.clip_attachments) {
        stream << '\n'
               << "clip[" << attachment.slot_index << "] " << attachment.slot_name
               << " -> attachment=" << attachment.attachment_name
               << ", end="
               << (attachment.end_slot_name.empty() ? std::string("<none>") : attachment.end_slot_name)
               << ", vertices=" << attachment.polygon.size();
    }

    for (const RegionAttachmentDrawCommand& attachment : scene_.region_attachments) {
        const RegionAttachmentVertex& min_corner = attachment.vertices[0];
        const RegionAttachmentVertex& max_corner = attachment.vertices[2];
        stream << '\n'
               << "slot[" << attachment.slot_index << "] " << attachment.slot_name
               << " -> attachment=" << attachment.attachment_name
               << ", region=" << attachment.atlas_region_name
               << ", bone=" << attachment.bone_index
               << ", blend=" << blend_mode_name(attachment.blend_mode)
               << ", rgba=(" << attachment.color.r << ", " << attachment.color.g << ", "
               << attachment.color.b << ", " << attachment.color.a << ")";
        if (attachment.dark_color.has_value()) {
            stream << ", darkRgba=(" << attachment.dark_color->r << ", "
                   << attachment.dark_color->g << ", " << attachment.dark_color->b << ", "
                   << attachment.dark_color->a << ")";
        } else {
            stream << ", darkRgba=(disabled)";
        }
        stream
               << ", quadMin=(" << min_corner.position.x << ", " << min_corner.position.y << ")"
               << ", quadMax=(" << max_corner.position.x << ", " << max_corner.position.y << ")"
               << ", maskedVertices=" << attachment.masked_vertices.size()
               << ", maskedTriangles=" << (attachment.masked_indices.size() / 3);
        if (attachment.clip_attachment_name.has_value()) {
            stream << ", clip=" << *attachment.clip_attachment_name;
        }
    }

    for (const DynamicMeshDrawCommand& attachment : scene_.dynamic_mesh_attachments) {
        const std::vector<SkinnedMeshVertex> skinned_vertices =
            evaluate_gpu_skinned_vertices(attachment, scene_.bone_palette);

        double min_x = 0.0;
        double min_y = 0.0;
        double max_x = 0.0;
        double max_y = 0.0;
        if (!skinned_vertices.empty()) {
            min_x = max_x = skinned_vertices.front().position.x;
            min_y = max_y = skinned_vertices.front().position.y;
            for (const SkinnedMeshVertex& vertex : skinned_vertices) {
                min_x = std::min(min_x, vertex.position.x);
                min_y = std::min(min_y, vertex.position.y);
                max_x = std::max(max_x, vertex.position.x);
                max_y = std::max(max_y, vertex.position.y);
            }
        }

        stream << '\n'
               << "slot[" << attachment.slot_index << "] " << attachment.slot_name
               << " -> attachment=" << attachment.attachment_name
               << ", region=" << attachment.atlas_region_name
               << ", buffer=" << mesh_buffer_usage_name(attachment.vertex_buffer_usage)
               << ", shader=" << mesh_shader_path_name(attachment.shader_path)
               << ", blend=" << blend_mode_name(attachment.blend_mode)
               << ", rgba=(" << attachment.color.r << ", " << attachment.color.g << ", "
               << attachment.color.b << ", " << attachment.color.a << ")";
        if (attachment.dark_color.has_value()) {
            stream << ", darkRgba=(" << attachment.dark_color->r << ", "
                   << attachment.dark_color->g << ", " << attachment.dark_color->b << ", "
                   << attachment.dark_color->a << ")";
        } else {
            stream << ", darkRgba=(disabled)";
        }
        stream
               << ", vertices=" << attachment.vertex_payloads.size()
               << ", triangles=" << (attachment.indices.size() / 3)
               << ", maskedVertices=" << attachment.masked_vertices.size()
               << ", maskedTriangles=" << (attachment.masked_indices.size() / 3)
               << ", boundsMin=(" << min_x << ", " << min_y << ")"
               << ", boundsMax=(" << max_x << ", " << max_y << ")";
        if (attachment.clip_attachment_name.has_value()) {
            stream << ", clip=" << *attachment.clip_attachment_name;
        }
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
