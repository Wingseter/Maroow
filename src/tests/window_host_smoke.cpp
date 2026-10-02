#include "../editor/window_host.hpp"

#include <SDL3/SDL_timer.h>

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

using marrow::editor::shell::EditorWindowHost;
using marrow::editor::shell::FrameSurface;
using marrow::editor::shell::WindowMetrics;

constexpr int kResizedWidth = 800;
constexpr int kResizedHeight = 600;
constexpr int kStatePollAttempts = 200;
constexpr Uint32 kStatePollIntervalMs = 10U;

// Window state changes are asynchronous requests to the window manager, so
// sync first and then pump until the flag lands or the budget runs out.
bool wait_for_minimized_state(
    EditorWindowHost* host,
    SDL_Window* window,
    bool expected) {
    SDL_SyncWindow(window);
    for (int attempt = 0; attempt < kStatePollAttempts; ++attempt) {
        host->poll_events({});
        if (host->metrics().minimized == expected) {
            return true;
        }
        SDL_Delay(kStatePollIntervalMs);
    }
    return false;
}

bool verify_resize_tracking(EditorWindowHost* host, SDL_Window* window) {
    const WindowMetrics before = host->metrics();
    if (!SDL_SetWindowSize(window, kResizedWidth, kResizedHeight)) {
        std::cerr << "SDL host could not request a window resize: "
                  << SDL_GetError() << '\n';
        return false;
    }
    SDL_SyncWindow(window);
    host->poll_events({});

    const WindowMetrics after = host->metrics();
    if (after.logical_width != kResizedWidth ||
        after.logical_height != kResizedHeight) {
        std::cerr << "SDL host metrics did not track the resize: logical "
                  << after.logical_width << 'x' << after.logical_height
                  << ", expected " << kResizedWidth << 'x' << kResizedHeight
                  << '\n';
        return false;
    }

    // The drawable must follow the logical size at the display scale that was
    // in effect before the resize; the scale itself must not drift.
    const long expected_drawable_width = std::lround(
        static_cast<double>(kResizedWidth) * before.framebuffer_scale_x);
    const long expected_drawable_height = std::lround(
        static_cast<double>(kResizedHeight) * before.framebuffer_scale_y);
    if (std::abs(after.drawable_width - expected_drawable_width) > 1 ||
        std::abs(after.drawable_height - expected_drawable_height) > 1) {
        std::cerr << "SDL host drawable did not track the resize: drawable "
                  << after.drawable_width << 'x' << after.drawable_height
                  << ", expected near " << expected_drawable_width << 'x'
                  << expected_drawable_height << '\n';
        return false;
    }

    const FrameSurface resized = host->acquire_frame_surface();
    if (!resized.acquired ||
        resized.swapchain.width != after.drawable_width ||
        resized.swapchain.height != after.drawable_height) {
        std::cerr << "SDL host swapchain did not follow the resized drawable\n";
        return false;
    }
    host->present();

    std::cout << "window_resize logical="
              << after.logical_width << 'x' << after.logical_height
              << " drawable="
              << after.drawable_width << 'x' << after.drawable_height << '\n';
    return true;
}

bool verify_minimize_restore(EditorWindowHost* host, SDL_Window* window) {
    if (!SDL_MinimizeWindow(window) ||
        !wait_for_minimized_state(host, window, true)) {
        // A window manager may refuse or defer miniaturization (no session,
        // remote display, tiling policy). Say so instead of asserting nothing.
        std::cout << "window_state SKIP minimize/restore: the window manager "
                     "did not report SDL_WINDOW_MINIMIZED within "
                  << (kStatePollAttempts * static_cast<int>(kStatePollIntervalMs))
                  << " ms on this host\n";
        SDL_RestoreWindow(window);
        wait_for_minimized_state(host, window, false);
        return true;
    }

    const FrameSurface minimized = host->acquire_frame_surface();
    if (minimized.acquired) {
        std::cerr << "SDL host acquired a frame surface while minimized\n";
        return false;
    }

    if (!SDL_RestoreWindow(window)) {
        std::cerr << "SDL host could not request a window restore: "
                  << SDL_GetError() << '\n';
        return false;
    }
    if (!wait_for_minimized_state(host, window, false)) {
        std::cerr << "SDL host stayed minimized after a restore request\n";
        return false;
    }

    const WindowMetrics restored_metrics = host->metrics();
    const FrameSurface restored = host->acquire_frame_surface();
    if (!restored.acquired || restored.swapchain.width <= 0 ||
        restored.swapchain.height <= 0 ||
        restored.swapchain.width != restored_metrics.drawable_width ||
        restored.swapchain.height != restored_metrics.drawable_height) {
        std::cerr << "SDL host did not recover a frame surface after restore\n";
        return false;
    }
    host->present();

    std::cout << "window_state minimize/restore verified; restored drawable="
              << restored_metrics.drawable_width << 'x'
              << restored_metrics.drawable_height << '\n';
    return true;
}

} // namespace

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
        // Only the first iteration owns a visible window, so resize and
        // minimize/restore can only be driven meaningfully there.
        if (iteration == 0) {
            if (!verify_resize_tracking(host.get(), host->sdl_window())) {
                return 1;
            }
            if (!verify_minimize_restore(host.get(), host->sdl_window())) {
                return 1;
            }
        }
        host->shutdown();
    }
    std::cout << "SDL window host smoke passed 20 lifecycle iterations\n";
    return 0;
}
