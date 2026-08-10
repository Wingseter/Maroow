#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "marrow/renderer/module.hpp"

namespace marrow::renderer::internal {
class SokolSceneRenderer;
}

namespace marrow::editor {

class ViewportRenderer {
public:
    struct Submission {
        renderer::RenderCommandList command_list;
    };

    ViewportRenderer();
    ViewportRenderer(const ViewportRenderer&) = delete;
    ViewportRenderer& operator=(const ViewportRenderer&) = delete;
    ViewportRenderer(ViewportRenderer&&) = delete;
    ViewportRenderer& operator=(ViewportRenderer&&) = delete;
    ~ViewportRenderer();

    std::optional<std::string> initialize(
        std::uint32_t color_format,
        std::uint32_t depth_format,
        int sample_count);
    void destroy();

    std::optional<std::string> prepare(
        const renderer::PreparedScene& scene,
        const std::filesystem::path& atlas_image_path,
        const std::array<float, 16>& projection,
        const std::array<float, 4>& tint_color,
        Submission* submission_out);
    std::optional<std::string> preflight(
        const std::vector<const Submission*>& submissions);
    std::optional<std::string> submit(const Submission& submission);

    bool available() const;
    const std::string& error_message() const;

private:
    std::optional<std::string> ensure_atlas_resources(
        const renderer::PreparedScene& scene,
        const std::filesystem::path& atlas_image_path);

    std::unique_ptr<renderer::internal::SokolSceneRenderer> scene_renderer_;
    std::filesystem::path atlas_texture_path_{};
    std::optional<std::filesystem::file_time_type> atlas_texture_write_time_;
    std::string atlas_filter_min_;
    std::string atlas_filter_mag_;
    std::string atlas_wrap_x_;
    std::string atlas_wrap_y_;
    bool atlas_premultiplied_alpha_{false};
    std::uint32_t color_format_{0};
    std::uint32_t depth_format_{0};
    int sample_count_{1};
    bool available_{false};
    std::string error_message_;
};

} // namespace marrow::editor
