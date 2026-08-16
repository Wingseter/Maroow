#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include "sokol_gfx.h"
#include "windowing.hpp"

namespace marrow::editor::shell {

struct WindowHostConfig {
    int logical_width{1440};
    int logical_height{900};
    std::string title{"Marrow"};
    bool resizable{true};
    bool high_dpi{true};
    bool visible{true};
    bool vsync{true};
    RendererSurface renderer_surface{RendererSurface::OpenGL};
    int requested_sample_count{4};
};

struct FrameSurface {
    bool acquired{false};
    sg_swapchain swapchain{};
};

class EditorWindowHost {
public:
    using EventCallback = std::function<void(const SDL_Event&)>;

    virtual ~EditorWindowHost() = default;
    virtual std::optional<std::string> initialize(
        const WindowHostConfig& config) = 0;
    virtual void poll_events(const EventCallback& callback) = 0;
    virtual WindowMetrics metrics() const = 0;
    virtual bool should_close() const noexcept = 0;
    virtual void request_close() noexcept = 0;
    virtual sg_environment graphics_environment() const noexcept = 0;
    virtual FrameSurface acquire_frame_surface() = 0;
    virtual void present() = 0;
    virtual void shutdown() noexcept = 0;
    virtual SDL_Window* sdl_window() const noexcept = 0;
    virtual SDL_GLContext gl_context() const noexcept = 0;
};

std::unique_ptr<EditorWindowHost> create_sdl_window_host();

} // namespace marrow::editor::shell
