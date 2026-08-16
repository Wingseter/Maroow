#include "window_host.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

#include <SDL3/SDL.h>

#if defined(__APPLE__)
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL3/SDL_metal.h>
#endif

namespace marrow::editor::shell {

namespace {

SDL_WindowFlags common_window_flags(const WindowHostConfig& config) {
    SDL_WindowFlags flags = 0;
    if (config.resizable) {
        flags |= SDL_WINDOW_RESIZABLE;
    }
    if (config.high_dpi) {
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    }
    if (!config.visible) {
        flags |= SDL_WINDOW_HIDDEN;
    }
    return flags;
}

class SdlWindowHost final : public EditorWindowHost {
public:
    ~SdlWindowHost() override { shutdown(); }

    std::optional<std::string> initialize(
        const WindowHostConfig& config) override {
        shutdown();
        config_ = config;
        SDL_SetHint(SDL_HINT_PEN_MOUSE_EVENTS, "1");
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return std::string("SDL video initialization failed: ") + SDL_GetError();
        }
        initialized_ = true;

#if defined(__APPLE__)
        if (config.renderer_surface != RendererSurface::Metal) {
            shutdown();
            return "The macOS editor requires an SDL Metal surface.";
        }
        if (const auto error = initialize_metal(config)) {
            shutdown();
            return error;
        }
#else
        if (config.renderer_surface != RendererSurface::OpenGL) {
            shutdown();
            return "This editor platform requires an SDL OpenGL 4.1 surface.";
        }
        if (const auto error = initialize_opengl(config)) {
            shutdown();
            return error;
        }
#endif

        close_requested_ = false;
        return std::nullopt;
    }

    void poll_events(const EventCallback& callback) override {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (callback) {
                callback(event);
            }
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 (window_ == nullptr ||
                  event.window.windowID == SDL_GetWindowID(window_)))) {
                close_requested_ = true;
            }
        }
    }

    WindowMetrics metrics() const override {
        if (window_ == nullptr) {
            return {};
        }
        int logical_width = 0;
        int logical_height = 0;
        int drawable_width = 0;
        int drawable_height = 0;
        SDL_GetWindowSize(window_, &logical_width, &logical_height);
        SDL_GetWindowSizeInPixels(window_, &drawable_width, &drawable_height);
        const SDL_WindowFlags flags = SDL_GetWindowFlags(window_);
        return make_window_metrics(
            logical_width,
            logical_height,
            drawable_width,
            drawable_height,
            SDL_GetWindowDisplayScale(window_),
            (flags & SDL_WINDOW_INPUT_FOCUS) != 0,
            (flags & SDL_WINDOW_MINIMIZED) != 0);
    }

    bool should_close() const noexcept override { return close_requested_; }
    void request_close() noexcept override { close_requested_ = true; }

    sg_environment graphics_environment() const noexcept override {
        sg_environment environment{};
        environment.defaults.color_format = color_format_;
        environment.defaults.depth_format = depth_format_;
        environment.defaults.sample_count = sample_count_;
#if defined(__APPLE__)
        if (config_.renderer_surface == RendererSurface::Metal) {
            environment.metal.device = (__bridge const void*)metal_device_;
        }
#endif
        return environment;
    }

    FrameSurface acquire_frame_surface() override {
        FrameSurface surface;
        const WindowMetrics current_metrics = metrics();
        if (window_ == nullptr || current_metrics.minimized ||
            current_metrics.drawable_width <= 0 ||
            current_metrics.drawable_height <= 0) {
            return surface;
        }

        surface.swapchain.width = current_metrics.drawable_width;
        surface.swapchain.height = current_metrics.drawable_height;
        surface.swapchain.color_format = color_format_;
        surface.swapchain.depth_format = depth_format_;
        surface.swapchain.sample_count = sample_count_;

#if defined(__APPLE__)
        if (!ensure_metal_attachments(
                current_metrics.drawable_width,
                current_metrics.drawable_height)) {
            return {};
        }
        current_drawable_ = [metal_layer_ nextDrawable];
        if (current_drawable_ == nil) {
            return {};
        }
        surface.swapchain.metal.current_drawable =
            (__bridge const void*)current_drawable_;
        surface.swapchain.metal.depth_stencil_texture =
            (__bridge const void*)depth_stencil_texture_;
        surface.swapchain.metal.msaa_color_texture =
            (__bridge const void*)msaa_color_texture_;
        surface.acquired = true;
#else
        surface.swapchain.gl.framebuffer = 0U;
        surface.acquired = true;
#endif
        return surface;
    }

    void present() override {
#if defined(__APPLE__)
        current_drawable_ = nil;
#else
        if (window_ != nullptr && gl_context_ != nullptr) {
            SDL_GL_SwapWindow(window_);
        }
#endif
    }

    void shutdown() noexcept override {
        close_requested_ = true;
#if defined(__APPLE__)
        current_drawable_ = nil;
        msaa_color_texture_ = nil;
        depth_stencil_texture_ = nil;
        metal_layer_ = nil;
        metal_device_ = nil;
        attachment_width_ = 0;
        attachment_height_ = 0;
        if (metal_view_ != nullptr) {
            SDL_Metal_DestroyView(metal_view_);
            metal_view_ = nullptr;
        }
#endif
#if !defined(__APPLE__)
        if (gl_context_ != nullptr) {
            SDL_GL_DestroyContext(gl_context_);
            gl_context_ = nullptr;
        }
#endif
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (initialized_) {
            SDL_Quit();
            initialized_ = false;
        }
        color_format_ = SG_PIXELFORMAT_NONE;
        depth_format_ = SG_PIXELFORMAT_NONE;
        sample_count_ = 1;
    }

    SDL_Window* sdl_window() const noexcept override { return window_; }
    SDL_GLContext gl_context() const noexcept override {
#if defined(__APPLE__)
        return nullptr;
#else
        return gl_context_;
#endif
    }

private:
#if !defined(__APPLE__)
    std::optional<std::string> configure_gl_attributes(int sample_count) {
        constexpr int kContextMajor = 4;
        constexpr int kContextMinor = 1;
        if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, kContextMajor) ||
            !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, kContextMinor) ||
            !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE) ||
            !SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0) ||
            !SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1) ||
            !SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24) ||
            !SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8) ||
            !SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, sample_count > 1 ? 1 : 0) ||
            !SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, sample_count > 1 ? sample_count : 0)) {
            return std::string("SDL OpenGL attribute setup failed: ") + SDL_GetError();
        }
        return std::nullopt;
    }

    bool try_create_opengl_window(const WindowHostConfig& config, int sample_count) {
        if (configure_gl_attributes(sample_count).has_value()) {
            return false;
        }
        window_ = SDL_CreateWindow(
            config.title.c_str(),
            config.logical_width,
            config.logical_height,
            common_window_flags(config) | SDL_WINDOW_OPENGL);
        if (window_ == nullptr) {
            return false;
        }
        gl_context_ = SDL_GL_CreateContext(window_);
        if (gl_context_ == nullptr || !SDL_GL_MakeCurrent(window_, gl_context_)) {
            if (gl_context_ != nullptr) {
                SDL_GL_DestroyContext(gl_context_);
                gl_context_ = nullptr;
            }
            SDL_DestroyWindow(window_);
            window_ = nullptr;
            return false;
        }
        return true;
    }

    std::optional<std::string> initialize_opengl(const WindowHostConfig& config) {
        const int requested_samples = std::max(config.requested_sample_count, 1);
        if (!try_create_opengl_window(config, requested_samples)) {
            const std::string first_error = SDL_GetError();
            if (requested_samples == 1 || !try_create_opengl_window(config, 1)) {
                return std::string("SDL OpenGL 4.1 context creation failed: ") +
                    (first_error.empty() ? SDL_GetError() : first_error);
            }
            std::cerr << "Warning: SDL OpenGL 4x MSAA was unavailable; using 1x.\n";
        }

        if (!SDL_GL_SetSwapInterval(config.vsync ? 1 : 0)) {
            std::cerr << "Warning: failed to set SDL swap interval: "
                      << SDL_GetError() << '\n';
        }
        int actual_samples = 1;
        int multisample_buffers = 0;
        int multisample_samples = 0;
        if (SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS, &multisample_buffers) &&
            SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &multisample_samples) &&
            multisample_buffers > 0 && multisample_samples > 1) {
            actual_samples = multisample_samples;
        }
        color_format_ = SG_PIXELFORMAT_RGBA8;
        depth_format_ = SG_PIXELFORMAT_DEPTH_STENCIL;
        sample_count_ = actual_samples;
        return std::nullopt;
    }
#endif

#if defined(__APPLE__)
    std::optional<std::string> initialize_metal(const WindowHostConfig& config) {
        window_ = SDL_CreateWindow(
            config.title.c_str(),
            config.logical_width,
            config.logical_height,
            common_window_flags(config) | SDL_WINDOW_METAL);
        if (window_ == nullptr) {
            return std::string("SDL Metal window creation failed: ") + SDL_GetError();
        }

        metal_view_ = SDL_Metal_CreateView(window_);
        if (metal_view_ == nullptr) {
            return std::string("SDL Metal view creation failed: ") + SDL_GetError();
        }
        metal_layer_ = (__bridge CAMetalLayer*)SDL_Metal_GetLayer(metal_view_);
        metal_device_ = MTLCreateSystemDefaultDevice();
        if (metal_layer_ == nil || metal_device_ == nil) {
            return "Failed to create the SDL Metal layer or Metal device.";
        }

        metal_layer_.device = metal_device_;
        metal_layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
        metal_layer_.framebufferOnly = YES;
        metal_layer_.opaque = YES;
        metal_layer_.displaySyncEnabled = config.vsync ? YES : NO;

        const int requested_samples = std::max(config.requested_sample_count, 1);
        if (requested_samples > 1 &&
            [metal_device_ supportsTextureSampleCount:requested_samples]) {
            sample_count_ = requested_samples;
        } else {
            sample_count_ = 1;
            if (requested_samples > 1) {
                std::cerr << "Warning: Metal 4x MSAA was unavailable; using 1x.\n";
            }
        }
        color_format_ = SG_PIXELFORMAT_BGRA8;
        depth_format_ = SG_PIXELFORMAT_DEPTH_STENCIL;
        return std::nullopt;
    }

    bool ensure_metal_attachments(int width, int height) {
        if (metal_layer_ == nil || metal_device_ == nil || width <= 0 || height <= 0) {
            return false;
        }
        if (attachment_width_ == width && attachment_height_ == height &&
            depth_stencil_texture_ != nil &&
            (sample_count_ == 1 || msaa_color_texture_ != nil)) {
            return true;
        }

        metal_layer_.drawableSize = CGSizeMake(width, height);

        MTLTextureDescriptor* depth_desc = [MTLTextureDescriptor new];
        depth_desc.textureType = sample_count_ > 1
            ? MTLTextureType2DMultisample
            : MTLTextureType2D;
        depth_desc.pixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        depth_desc.width = static_cast<NSUInteger>(width);
        depth_desc.height = static_cast<NSUInteger>(height);
        depth_desc.mipmapLevelCount = 1;
        depth_desc.sampleCount = static_cast<NSUInteger>(sample_count_);
        depth_desc.storageMode = MTLStorageModePrivate;
        depth_desc.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> new_depth = [metal_device_ newTextureWithDescriptor:depth_desc];
        if (new_depth == nil) {
            return false;
        }

        id<MTLTexture> new_msaa = nil;
        if (sample_count_ > 1) {
            MTLTextureDescriptor* color_desc = [MTLTextureDescriptor new];
            color_desc.textureType = MTLTextureType2DMultisample;
            color_desc.pixelFormat = MTLPixelFormatBGRA8Unorm;
            color_desc.width = static_cast<NSUInteger>(width);
            color_desc.height = static_cast<NSUInteger>(height);
            color_desc.mipmapLevelCount = 1;
            color_desc.sampleCount = static_cast<NSUInteger>(sample_count_);
            color_desc.storageMode = MTLStorageModePrivate;
            color_desc.usage = MTLTextureUsageRenderTarget;
            new_msaa = [metal_device_ newTextureWithDescriptor:color_desc];
            if (new_msaa == nil) {
                return false;
            }
        }

        depth_stencil_texture_ = new_depth;
        msaa_color_texture_ = new_msaa;
        attachment_width_ = width;
        attachment_height_ = height;
        return true;
    }
#endif

    WindowHostConfig config_{};
    SDL_Window* window_{nullptr};
#if !defined(__APPLE__)
    SDL_GLContext gl_context_{nullptr};
#endif
    bool initialized_{false};
    bool close_requested_{true};
    sg_pixel_format color_format_{SG_PIXELFORMAT_NONE};
    sg_pixel_format depth_format_{SG_PIXELFORMAT_NONE};
    int sample_count_{1};

#if defined(__APPLE__)
    SDL_MetalView metal_view_{nullptr};
    CAMetalLayer* metal_layer_{nil};
    id<MTLDevice> metal_device_{nil};
    id<CAMetalDrawable> current_drawable_{nil};
    id<MTLTexture> depth_stencil_texture_{nil};
    id<MTLTexture> msaa_color_texture_{nil};
    int attachment_width_{0};
    int attachment_height_{0};
#endif
};

} // namespace

std::unique_ptr<EditorWindowHost> create_sdl_window_host() {
    return std::make_unique<SdlWindowHost>();
}

} // namespace marrow::editor::shell
