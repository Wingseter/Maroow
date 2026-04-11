#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include "marrow/renderer/module.hpp"

namespace marrow::editor {

class ViewportRenderer {
public:
    struct ActiveStencilClip {
        std::size_t clip_attachment_index{0};
        std::uint8_t reference_value{0};
        std::uint8_t parent_reference_value{0};
        std::uint8_t invert_mask{0};
    };

    ViewportRenderer() = default;
    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;
    ViewportRenderer(ViewportRenderer&&) = delete;
    ViewportRenderer& operator=(ViewportRenderer&&) = delete;
    ~ViewportRenderer() = default;

    std::optional<std::string> initialize();
    void destroy();
    std::optional<std::string> render(
        const renderer::PreparedScene& scene,
        const std::filesystem::path& atlas_image_path,
        const std::array<float, 16>& projection);
    std::optional<std::string> render_tinted(
        const renderer::PreparedScene& scene,
        const std::filesystem::path& atlas_image_path,
        const std::array<float, 16>& projection,
        const std::array<float, 4>& tint_color);

    bool available() const;
    const std::string& error_message() const;

private:
    struct ProgramHandles {
        GLuint program{0};
        GLuint vertex_shader{0};
        GLuint fragment_shader{0};
        GLint projection_location{-1};
        GLint bones_location{-1};
        GLint atlas_texture_location{-1};
        GLint pma_control_location{-1};
    };

    std::optional<std::string> initialize_programs();
    void destroy_program(ProgramHandles* program);
    std::optional<std::string> create_program(
        const char* vertex_source,
        const char* fragment_source,
        ProgramHandles* program_out);
    std::optional<std::string> ensure_textures();
    void destroy_textures();
    std::optional<std::string> ensure_atlas_texture(
        const renderer::PreparedScene& scene,
        const std::filesystem::path& atlas_image_path);
    std::optional<std::string> upload_atlas_texture(
        const renderer::PreparedScene& scene,
        const std::filesystem::path& atlas_image_path);
    std::optional<std::string> submit_command_list(
        const renderer::RenderCommandList& command_list);
    std::optional<std::string> submit_draw_command(
        const renderer::RenderCommand& command,
        const renderer::RenderCommandList& command_list,
        std::optional<std::uint8_t> stencil_reference,
        const std::array<float, 192U * 4U>& packed_bones);
    std::optional<std::string> submit_clip_command(
        const renderer::RenderClipCommand& clip_command,
        const renderer::RenderCommandList& command_list,
        const ActiveStencilClip& clip,
        bool restoring_parent_reference,
        const std::array<float, 192U * 4U>& packed_bones);
    std::optional<std::string> upload_geometry(
        const std::vector<renderer::RenderCommandVertex>& vertices,
        const std::vector<std::uint32_t>& indices);
    void configure_draw_state(
        renderer::ColorShaderVariant shader_variant,
        renderer::BlendState blend_state,
        bool premultiplied_alpha,
        std::optional<std::uint8_t> stencil_reference,
        const renderer::RenderCommandList& command_list,
        const std::array<float, 192U * 4U>& packed_bones);
    void configure_clip_state(
        const ActiveStencilClip& clip,
        bool restoring_parent_reference,
        const renderer::RenderCommandList& command_list,
        const std::array<float, 192U * 4U>& packed_bones);

    ProgramHandles single_color_program_{};
    ProgramHandles two_color_program_{};
    GLuint scene_vao_{0};
    GLuint scene_vbo_{0};
    GLuint scene_ebo_{0};
    GLuint atlas_texture_{0};
    GLuint white_texture_{0};
    std::filesystem::path atlas_texture_path_{};
    std::optional<std::filesystem::file_time_type> atlas_texture_write_time_;
    std::string atlas_filter_min_;
    std::string atlas_filter_mag_;
    std::string atlas_wrap_x_;
    std::string atlas_wrap_y_;
    bool atlas_premultiplied_alpha_{false};
    bool available_{false};
    std::string error_message_;
};

} // namespace marrow::editor
