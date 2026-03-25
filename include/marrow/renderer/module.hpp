#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace marrow::renderer {

inline constexpr std::size_t kMaxGpuSkinningInfluences = 4;

struct SampleAppWindow {
    std::string title{"Marrow Renderer Sample"};
    int width{1280};
    int height{720};
};

struct RenderPoint {
    double x{0.0};
    double y{0.0};
};

struct RegionAttachmentVertex {
    RenderPoint position;
    RenderPoint uv;
};

struct RegionAttachmentDrawCommand {
    std::string slot_name;
    std::string attachment_name;
    std::string atlas_region_name;
    std::size_t slot_index{0};
    std::size_t bone_index{0};
    runtime::BlendMode blend_mode{runtime::BlendMode::Normal};
    runtime::SlotColor color{};
    std::optional<runtime::SlotColor> dark_color;
    std::optional<std::string> clip_attachment_name;
    std::array<RegionAttachmentVertex, 4> vertices{};
    std::vector<RegionAttachmentVertex> masked_vertices;
    std::vector<std::size_t> masked_indices;
};

enum class MeshBufferUsage {
    Static,
    Dynamic,
};

enum class MeshShaderPath {
    GpuSkinning,
};

struct GpuSkinningVertexPayload {
    std::array<RenderPoint, kMaxGpuSkinningInfluences> bone_local_positions{};
    std::array<std::size_t, kMaxGpuSkinningInfluences> bone_indices{};
    std::array<double, kMaxGpuSkinningInfluences> bone_weights{};
    std::size_t influence_count{0};
    RenderPoint uv;
};

struct SkinnedMeshVertex {
    RenderPoint position;
    RenderPoint uv;
};

struct DynamicMeshDrawCommand {
    std::string slot_name;
    std::string attachment_name;
    std::string atlas_region_name;
    std::size_t slot_index{0};
    runtime::BlendMode blend_mode{runtime::BlendMode::Normal};
    runtime::SlotColor color{};
    std::optional<runtime::SlotColor> dark_color;
    std::optional<std::string> clip_attachment_name;
    MeshBufferUsage vertex_buffer_usage{MeshBufferUsage::Dynamic};
    MeshShaderPath shader_path{MeshShaderPath::GpuSkinning};
    std::vector<GpuSkinningVertexPayload> vertex_payloads;
    std::vector<std::size_t> indices;
    std::vector<SkinnedMeshVertex> masked_vertices;
    std::vector<std::size_t> masked_indices;
};

struct ClipAttachmentDrawCommand {
    std::string slot_name;
    std::string attachment_name;
    std::size_t slot_index{0};
    std::optional<std::size_t> end_slot_index;
    std::string end_slot_name;
    std::vector<RenderPoint> polygon;
};

struct PreparedScene {
    std::string atlas_name;
    std::string atlas_image;
    std::string skeleton_name;
    std::vector<runtime::BoneWorldTransform> bone_palette;
    std::vector<ClipAttachmentDrawCommand> clip_attachments;
    std::vector<RegionAttachmentDrawCommand> region_attachments;
    std::vector<DynamicMeshDrawCommand> dynamic_mesh_attachments;
};

struct PreparedSceneResult {
    std::optional<PreparedScene> scene;
    std::string error_message;

    explicit operator bool() const {
        return scene.has_value();
    }
};

PreparedSceneResult prepare_setup_pose_scene(
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas);
std::vector<SkinnedMeshVertex> evaluate_gpu_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette);

class DemoShell {
public:
    DemoShell(SampleAppWindow window, PreparedScene scene);

    std::string launch_report() const;

private:
    SampleAppWindow window_;
    PreparedScene scene_;
};

std::string_view component_name();
std::string_view validation_target_name();

} // namespace marrow::renderer
