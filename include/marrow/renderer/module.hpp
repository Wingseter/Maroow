#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
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

using TextureHandle = std::uint64_t;

inline constexpr TextureHandle kAtlasTextureHandle = 1U;
inline constexpr TextureHandle kSolidWhiteTextureHandle = 2U;

struct BatchBreakReasons {
    std::size_t texture_changes{0};
    std::size_t blend_changes{0};
    std::size_t clip_changes{0};
    std::size_t shader_changes{0};
};

struct RenderCommandVertex {
    std::array<std::array<float, 2>, kMaxGpuSkinningInfluences> local_positions{};
    std::array<float, 2> uv{};
    std::array<float, kMaxGpuSkinningInfluences> bone_indices{};
    std::array<float, kMaxGpuSkinningInfluences> bone_weights{};
    std::array<float, 4> light_color{};
    std::array<std::uint8_t, 4> dark_color{};
};

struct RenderCommand {
    std::vector<RenderCommandVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string texture_name;
    TextureHandle texture_handle{kAtlasTextureHandle};
    runtime::BlendMode blend_mode{runtime::BlendMode::Normal};
    ColorShaderVariant shader_variant{ColorShaderVariant::SingleColor};
    std::size_t source_draw_command_offset{0};
    std::size_t source_draw_command_count{0};
};

struct RenderClipCommand {
    std::vector<RenderCommandVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::string attachment_name;
    std::size_t source_clip_attachment_index{0};
};

enum class RenderCommandEventKind {
    ClipStart,
    Draw,
    ClipEnd,
};

struct RenderCommandEventRef {
    RenderCommandEventKind kind{RenderCommandEventKind::Draw};
    std::size_t index{0};
};

struct RenderCommandList {
    std::vector<RenderCommand> commands;
    std::vector<RenderClipCommand> clip_commands;
    std::vector<RenderCommandEventRef> ordered_events;
    std::vector<float> bone_palette;
    std::array<float, 16> projection{};
    bool premultiplied_alpha{false};
    BatchBreakReasons batch_break_reasons{};
};

struct RenderCommandListResult {
    std::optional<RenderCommandList> command_list;
    std::string error_message;

    /// @brief Reports whether render-command-list construction succeeded.
    /// @return `true` when a command list is present; otherwise `false`.
    explicit operator bool() const {
        return command_list.has_value();
    }
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

    /// @brief Reports whether GPU skinning evaluation succeeded.
    /// @return `true` when no error message is present; otherwise `false`.
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
    std::size_t skeleton_count{0};
    std::size_t draw_command_count{0};
    std::size_t vertex_count{0};
    std::size_t index_count{0};
    std::size_t draw_call_count{0};
    std::size_t merged_draw_calls{0};
    std::size_t bone_uniform_count{0};
    std::size_t vertex_buffer_bytes{0};
    std::size_t index_buffer_bytes{0};
    BatchBreakReasons break_reasons{};
    std::optional<std::string> error_message;

    /// @brief Reports whether batch summarization succeeded.
    /// @return `true` when no error message is present; otherwise `false`.
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
    std::size_t skeleton_count{0};
    std::vector<runtime::BoneWorldTransform> bone_palette;
    std::vector<ClipAttachmentDrawCommand> clip_attachments;
    std::vector<PreparedDrawCommand> draw_commands;
    std::vector<PreparedSceneEventRef> ordered_events;
};

struct PreparedSceneResult {
    std::optional<PreparedScene> scene;
    std::string error_message;

    /// @brief Reports whether prepared-scene construction succeeded.
    /// @return `true` when a prepared scene is present; otherwise `false`.
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

struct BackendCreateInfo {
    SampleAppWindow window;
    TextureImage atlas_texture;
    std::string atlas_filter_min;
    std::string atlas_filter_mag;
    std::string atlas_wrap_x;
    std::string atlas_wrap_y;
    bool hidden_window{false};
};

struct BackendFrameInfo {
    int framebuffer_width{0};
    int framebuffer_height{0};
    bool should_close{false};
};

class Backend {
public:
    /// @brief Virtual destructor for renderer backend implementations.
    virtual ~Backend() = default;

    /**
     * @brief Creates backend resources for a prepared scene and atlas texture.
     * @param create_info Window, texture, and sampler settings for backend startup.
     * @return Error string on failure, or `std::nullopt` on success.
     */
    virtual std::optional<std::string> create(const BackendCreateInfo& create_info) = 0;
    /// @brief Destroys backend resources created by `create()`.
    virtual void destroy() = 0;
    /**
     * @brief Begins a frame and reports current framebuffer state.
     * @param frame_info_out Receives framebuffer dimensions and close state.
     * @return Error string on failure, or `std::nullopt` on success.
     */
    virtual std::optional<std::string> begin_frame(BackendFrameInfo* frame_info_out) = 0;
    /**
     * @brief Submits one render command list to the backend.
     * @param command_list Packed render commands generated from a prepared scene.
     * @return Error string on failure, or `std::nullopt` on success.
     */
    virtual std::optional<std::string> submit_commands(const RenderCommandList& command_list) = 0;
    /// @brief Ends the current frame and presents it when applicable.
    virtual void end_frame() = 0;
};

/**
 * @brief Builds a prepared scene from the skeleton's current pose and atlas metadata.
 * @param skeleton Skeleton instance containing the current pose.
 * @param atlas Atlas metadata used for region lookup and texture hints.
 * @return Prepared draw commands or an error string.
 */
PreparedSceneResult prepare_setup_pose_scene(
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas);
/**
 * @brief Converts a prepared scene into packed GPU submission data.
 * @param scene Prepared scene to convert.
 * @param projection Projection matrix applied during rendering.
 * @return Packed render command list or an error string.
 */
RenderCommandListResult build_render_command_list(
    const PreparedScene& scene,
    const std::array<float, 16>& projection);
/**
 * @brief Summarizes batching and packed-buffer sizes for one command list.
 * @param scene Prepared scene that produced the command list.
 * @param command_list Packed command list to summarize.
 * @return Batch summary or an error string.
 */
PreparedSceneBatchSummary summarize_render_command_list(
    const PreparedScene& scene,
    const RenderCommandList& command_list);
/**
 * @brief Evaluates GPU-skinned mesh vertices against a bone palette.
 * @param attachment Dynamic mesh draw command to evaluate.
 * @param bone_palette Bone world transforms used for skinning.
 * @return Evaluated world-space mesh vertices or an error string.
 */
GpuSkinningEvaluationResult evaluate_gpu_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette);
/**
 * @brief Summarizes batching opportunities from a prepared scene.
 * @param scene Prepared scene to summarize.
 * @return Batch summary or an error string.
 */
PreparedSceneBatchSummary summarize_prepared_scene_batches(const PreparedScene& scene);
/**
 * @brief Returns the region-attachment view of a prepared draw command.
 * @param command Variant draw command to inspect.
 * @return Region attachment pointer, or `nullptr` when the command is not a region draw.
 */
const RegionAttachmentDrawCommand* region_attachment_command(const PreparedDrawCommand& command);
/**
 * @brief Returns the dynamic-mesh view of a prepared draw command.
 * @param command Variant draw command to inspect.
 * @return Dynamic mesh pointer, or `nullptr` when the command is not a mesh draw.
 */
const DynamicMeshDrawCommand* dynamic_mesh_attachment_command(const PreparedDrawCommand& command);
/**
 * @brief Loads a PNG texture and falls back to a white texel when loading fails.
 * @param image_path Path to the atlas image.
 * @return Texture pixels plus a status message describing the result.
 */
TextureImageLoadResult load_png_texture_or_white(const std::filesystem::path& image_path);
/**
 * @brief Maps a Marrow blend mode to concrete source and destination factors.
 * @param blend_mode Blend mode requested by the slot.
 * @param premultiplied_alpha Whether the atlas texture uses premultiplied alpha.
 * @return Renderer blend factors for the requested mode.
 */
BlendState blend_state_for(runtime::BlendMode blend_mode, bool premultiplied_alpha);

class DemoShell {
public:
    /**
     * @brief Creates a convenience sample viewer for one prepared scene.
     * @param window Window title and dimensions.
     * @param scene Prepared scene to display.
     * @param atlas_image_path Path to the atlas texture image.
     * @param hud_overlay_enabled Whether the debug HUD should be displayed.
     */
    DemoShell(
        SampleAppWindow window,
        PreparedScene scene,
        std::filesystem::path atlas_image_path = {},
        bool hud_overlay_enabled = false);

    /// @brief Builds a textual launch report describing the prepared scene.
    /// @return Human-readable sample launch summary.
    std::string launch_report() const;
    /**
     * @brief Runs the sample viewer until the window closes or auto-close triggers.
     * @param auto_close_frames Optional frame count for hidden-window smoke validation.
     * @return Error string on failure, or `std::nullopt` on success.
     */
    std::optional<std::string> run(std::optional<int> auto_close_frames = std::nullopt) const;

private:
    SampleAppWindow window_;
    PreparedScene scene_;
    std::filesystem::path atlas_image_path_;
    bool hud_overlay_enabled_{false};
};

/// @brief Returns the renderer module name used by diagnostics and sample output.
std::string_view component_name();
/// @brief Returns the primary renderer validation target name used by sample output.
std::string_view validation_target_name();

} // namespace marrow::renderer
