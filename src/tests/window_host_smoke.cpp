#include "../editor/window_host.hpp"

#include <iostream>

int main() {
    using namespace marrow::editor::shell;
    for (int iteration = 0; iteration < 20; ++iteration) {
        auto host = create_sdl_window_host();
        WindowHostConfig config;
        config.logical_width = 640;
        config.logical_height = 480;
        config.title = "Marrow SDL Window Host Smoke";
        config.visible = iteration == 0;
        config.vsync = false;
#if defined(__APPLE__)
        config.renderer_surface = RendererSurface::Metal;
#else
        config.renderer_surface = RendererSurface::OpenGL;
#endif
        if (const auto error = host->initialize(config)) {
            std::cerr << *error << '\n';
            return 1;
        }
        host->poll_events({});
        const WindowMetrics metrics = host->metrics();
        if (metrics.logical_width <= 0 || metrics.logical_height <= 0 ||
            metrics.drawable_width <= 0 || metrics.drawable_height <= 0) {
            std::cerr << "SDL host did not expose valid logical and pixel metrics\n";
            return 1;
        }
        if (iteration == 0) {
            std::cout << "window_metrics logical="
                      << metrics.logical_width << 'x' << metrics.logical_height
                      << " drawable="
                      << metrics.drawable_width << 'x' << metrics.drawable_height
                      << " framebuffer_scale="
                      << metrics.framebuffer_scale_x << 'x'
                      << metrics.framebuffer_scale_y
                      << " display_content_scale="
                      << metrics.display_content_scale << '\n';
        }
        const sg_environment environment = host->graphics_environment();
        const FrameSurface surface = host->acquire_frame_surface();
        if (!surface.acquired || environment.defaults.color_format == SG_PIXELFORMAT_NONE ||
            surface.swapchain.width != metrics.drawable_width ||
            surface.swapchain.height != metrics.drawable_height) {
            std::cerr << "SDL host did not acquire a valid renderer surface\n";
            return 1;
        }
        if (iteration == 0) {
            std::cout << "surface color_format="
                      << static_cast<int>(surface.swapchain.color_format)
                      << " depth_format="
                      << static_cast<int>(surface.swapchain.depth_format)
                      << " sample_count=" << surface.swapchain.sample_count << '\n';
        }
        host->present();
        host->shutdown();
    }
    std::cout << "SDL window host smoke passed 20 lifecycle iterations\n";
    return 0;
}
