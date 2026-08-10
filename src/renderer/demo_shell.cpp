#include "marrow/renderer/module.hpp"

#include "module_internal.hpp"

#include <iostream>
#include <memory>
#include <utility>

namespace marrow::renderer {

std::optional<std::string> DemoShell::run(
    std::optional<int> auto_close_frames) const {
    if (scene_.draw_commands.empty()) {
        return "Prepared scene does not contain any attachments to render.";
    }

    const TextureImageLoadResult texture_image =
        load_png_texture_or_white(atlas_image_path_);
    if (!texture_image.loaded_from_file && !texture_image.message.empty()) {
        std::cerr << texture_image.message << '\n';
    }

    BackendCreateInfo create_info;
    create_info.window = window_;
    create_info.atlas_texture = texture_image.image;
    create_info.atlas_filter_min = scene_.atlas_filter_min;
    create_info.atlas_filter_mag = scene_.atlas_filter_mag;
    create_info.atlas_wrap_x = scene_.atlas_wrap_x;
    create_info.atlas_wrap_y = scene_.atlas_wrap_y;
    create_info.hidden_window = auto_close_frames.has_value();

    std::unique_ptr<Backend> backend = internal::make_sokol_backend();
    const auto render_frame = [&](const BackendFrameInfo& frame_info) {
        return internal::render_demo_frame(
            scene_,
            hud_overlay_enabled_,
            backend.get(),
            frame_info);
    };

#if defined(__APPLE__)
    if (auto_close_frames.has_value()) {
        if (const std::optional<std::string> error = backend->create(create_info)) {
            backend->destroy();
            return error;
        }

        for (int frame_index = 0; frame_index < *auto_close_frames; ++frame_index) {
            BackendFrameInfo frame_info;
            if (const std::optional<std::string> error =
                    backend->begin_frame(&frame_info)) {
                backend->destroy();
                return error;
            }
            if (const std::optional<std::string> error = render_frame(frame_info)) {
                backend->destroy();
                return error;
            }
            backend->end_frame();
        }

        backend->destroy();
        return std::nullopt;
    }
#endif

    return internal::run_sokol_app(
        create_info,
        backend.get(),
        render_frame,
        auto_close_frames);
}

} // namespace marrow::renderer
