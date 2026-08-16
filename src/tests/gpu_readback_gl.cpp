#include "gpu_readback.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <SDL3/SDL_opengl_glext.h>

namespace marrow::tests {

namespace {

template <typename Function>
Function load_gl_function(const char* name) {
    return reinterpret_cast<Function>(SDL_GL_GetProcAddress(name));
}

} // namespace

GpuReadbackResult read_sokol_rgba8_image(
    sg_image image,
    int width,
    int height) {
    GpuReadbackResult result;
    result.width = width;
    result.height = height;
    if (width <= 0 || height <= 0) {
        result.error = "GL readback requires positive dimensions.";
        return result;
    }

    const sg_gl_image_info image_info = sg_gl_query_image_info(image);
    if (image_info.active_slot < 0 ||
        image_info.active_slot >= SG_NUM_INFLIGHT_FRAMES ||
        image_info.tex[image_info.active_slot] == 0U) {
        result.error = "Sokol did not expose the GL texture for readback.";
        return result;
    }

    const auto gen_framebuffers =
        load_gl_function<PFNGLGENFRAMEBUFFERSPROC>("glGenFramebuffers");
    const auto bind_framebuffer =
        load_gl_function<PFNGLBINDFRAMEBUFFERPROC>("glBindFramebuffer");
    const auto framebuffer_texture_2d =
        load_gl_function<PFNGLFRAMEBUFFERTEXTURE2DPROC>("glFramebufferTexture2D");
    const auto check_framebuffer_status =
        load_gl_function<PFNGLCHECKFRAMEBUFFERSTATUSPROC>("glCheckFramebufferStatus");
    const auto delete_framebuffers =
        load_gl_function<PFNGLDELETEFRAMEBUFFERSPROC>("glDeleteFramebuffers");
    if (gen_framebuffers == nullptr || bind_framebuffer == nullptr ||
        framebuffer_texture_2d == nullptr || check_framebuffer_status == nullptr ||
        delete_framebuffers == nullptr) {
        result.error = "Required GL 4.1 framebuffer readback functions were unavailable.";
        return result;
    }

    GLint previous_framebuffer = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
    GLuint framebuffer = 0U;
    gen_framebuffers(1, &framebuffer);
    bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
    framebuffer_texture_2d(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        image_info.tex_target,
        image_info.tex[image_info.active_slot],
        0);
    if (check_framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        bind_framebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
        delete_framebuffers(1, &framebuffer);
        sg_reset_state_cache();
        result.error = "Temporary GL readback framebuffer was incomplete.";
        return result;
    }

    const std::size_t row_bytes = static_cast<std::size_t>(width) * 4U;
    std::vector<std::uint8_t> bottom_left(
        row_bytes * static_cast<std::size_t>(height));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(
        0,
        0,
        width,
        height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        bottom_left.data());

    bind_framebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
    delete_framebuffers(1, &framebuffer);
    sg_reset_state_cache();

    result.top_left_rgba8.resize(bottom_left.size());
    for (int row = 0; row < height; ++row) {
        const std::size_t source_offset =
            static_cast<std::size_t>(height - 1 - row) * row_bytes;
        const std::size_t destination_offset =
            static_cast<std::size_t>(row) * row_bytes;
        std::memcpy(
            result.top_left_rgba8.data() + destination_offset,
            bottom_left.data() + source_offset,
            row_bytes);
    }
    return result;
}

} // namespace marrow::tests
