#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

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
    std::string texture_name;
    std::size_t slot_index{0};
    std::size_t draw_order_index{0};
    std::size_t bone_index{0};
    runtime::BlendMode blend_mode{runtime::BlendMode::Normal};
    runtime::SlotColor color{};
    std::optional<runtime::SlotColor> dark_color;
    std::optional<std::string> clip_attachment_name;
    std::array<RenderPoint, 4> local_positions{};
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

enum class ColorShaderVariant {
    SingleColor,
    TwoColorTint,
};

enum class BlendFactor {
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstColor,
    OneMinusSrcColor,
};

struct BlendState {
    BlendFactor src_factor{BlendFactor::One};
    BlendFactor dst_factor{BlendFactor::Zero};
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

struct GpuSkinningEvaluationResult {
    std::vector<SkinnedMeshVertex> vertices;
    std::optional<std::string> error_message;

    explicit operator bool() const {
        return !error_message.has_value();
    }
};

struct DynamicMeshDrawCommand {
    std::string slot_name;
    std::string attachment_name;
    std::string atlas_region_name;
    std::string texture_name;
    std::size_t slot_index{0};
    std::size_t draw_order_index{0};
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
    std::size_t draw_order_index{0};
    std::optional<std::size_t> end_slot_index;
    std::string end_slot_name;
    std::vector<RenderPoint> polygon;
};

using PreparedDrawCommand = std::variant<RegionAttachmentDrawCommand, DynamicMeshDrawCommand>;

enum class PreparedSceneEventKind {
    ClipStart,
    Draw,
    ClipEnd,
};

struct PreparedSceneEventRef {
    PreparedSceneEventKind kind{PreparedSceneEventKind::Draw};
    std::size_t index{0};
};

struct PreparedDrawBatch {
    std::string texture_name;
    runtime::BlendMode blend_mode{runtime::BlendMode::Normal};
    ColorShaderVariant shader_variant{ColorShaderVariant::SingleColor};
    std::size_t draw_command_offset{0};
    std::size_t draw_command_count{0};
    std::size_t vertex_count{0};
    std::size_t index_count{0};
};

struct PreparedSceneBatchSummary {
    std::vector<PreparedDrawBatch> batches;
    std::size_t draw_command_count{0};
    std::size_t vertex_count{0};
    std::size_t index_count{0};
    std::size_t draw_call_count{0};
    std::size_t merged_draw_calls{0};
    std::size_t bone_uniform_count{0};
    std::size_t vertex_buffer_bytes{0};
    std::size_t index_buffer_bytes{0};
    std::optional<std::string> error_message;

    explicit operator bool() const {
        return !error_message.has_value();
    }
};

struct PreparedScene {
    std::string atlas_name;
    std::string atlas_image;
    std::string atlas_filter_min;
    std::string atlas_filter_mag;
    std::string atlas_wrap_x;
    std::string atlas_wrap_y;
    bool premultiplied_alpha{false};
    std::string skeleton_name;
    std::vector<runtime::BoneWorldTransform> bone_palette;
    std::vector<ClipAttachmentDrawCommand> clip_attachments;
    std::vector<PreparedDrawCommand> draw_commands;
    std::vector<PreparedSceneEventRef> ordered_events;
};

struct PreparedSceneResult {
    std::optional<PreparedScene> scene;
    std::string error_message;

    explicit operator bool() const {
        return scene.has_value();
    }
};

struct TextureImage {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba8;
};

struct TextureImageLoadResult {
    TextureImage image;
    bool loaded_from_file{false};
    std::string message;
};

PreparedSceneResult prepare_setup_pose_scene(
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas);
GpuSkinningEvaluationResult evaluate_gpu_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette);
PreparedSceneBatchSummary summarize_prepared_scene_batches(const PreparedScene& scene);
const RegionAttachmentDrawCommand* region_attachment_command(const PreparedDrawCommand& command);
const DynamicMeshDrawCommand* dynamic_mesh_attachment_command(const PreparedDrawCommand& command);
TextureImageLoadResult load_png_texture_or_white(const std::filesystem::path& image_path);
BlendState blend_state_for(runtime::BlendMode blend_mode, bool premultiplied_alpha);

class DemoShell {
public:
    DemoShell(
        SampleAppWindow window,
        PreparedScene scene,
        std::filesystem::path atlas_image_path = {});

    std::string launch_report() const;
    std::optional<std::string> run(std::optional<int> auto_close_frames = std::nullopt) const;

private:
    SampleAppWindow window_;
    PreparedScene scene_;
    std::filesystem::path atlas_image_path_;
};

std::string_view component_name();
std::string_view validation_target_name();

} // namespace marrow::renderer
