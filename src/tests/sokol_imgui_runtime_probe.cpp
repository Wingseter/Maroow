#include <cstdint>
#include <iostream>

#define SOKOL_DUMMY_BACKEND
#define SOKOL_GFX_IMPL
#define SOKOL_IMGUI_IMPL
#define SOKOL_IMGUI_NO_SOKOL_APP

#include "sokol_gfx.h"
#include "imgui.h"
#include "sokol_imgui.h"

namespace {

int validation_errors = 0;

void sg_log(
    const char*,
    std::uint32_t level,
    std::uint32_t,
    const char* message,
    std::uint32_t,
    const char*,
    void*) {
    if (level <= 1U) {
        ++validation_errors;
        std::cerr << "sokol_gfx validation: "
                  << (message == nullptr ? "unknown error" : message) << '\n';
    }
}

void simgui_log(
    const char*,
    std::uint32_t level,
    std::uint32_t,
    const char* message,
    std::uint32_t,
    const char*,
    void*) {
    if (level <= 1U) {
        ++validation_errors;
        std::cerr << "sokol_imgui validation: "
                  << (message == nullptr ? "unknown error" : message) << '\n';
    }
}

} // namespace

int main() {
    sg_desc graphics_descriptor{};
    graphics_descriptor.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    graphics_descriptor.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    graphics_descriptor.environment.defaults.sample_count = 1;
    graphics_descriptor.logger.func = sg_log;
    sg_setup(&graphics_descriptor);
    if (!sg_isvalid()) {
        std::cerr << "sokol_gfx dummy setup failed\n";
        return 1;
    }

    simgui_desc_t imgui_descriptor{};
    imgui_descriptor.color_format = SG_PIXELFORMAT_RGBA8;
    imgui_descriptor.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    imgui_descriptor.sample_count = 1;
    imgui_descriptor.logger.func = simgui_log;
    simgui_setup(&imgui_descriptor);

    simgui_frame_desc_t frame{};
    frame.width = 64;
    frame.height = 64;
    frame.delta_time = 1.0 / 60.0;
    frame.dpi_scale = 1.0f;
    simgui_new_frame(&frame);
    ImGui::Begin("probe");
    ImGui::TextUnformatted("one frame");
    ImGui::End();

    sg_pass pass{};
    pass.swapchain.width = 64;
    pass.swapchain.height = 64;
    pass.swapchain.sample_count = 1;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    sg_begin_pass(&pass);
    simgui_render();
    sg_end_pass();
    sg_commit();

    simgui_shutdown();
    sg_shutdown();
    if (validation_errors != 0) {
        std::cerr << "sokol_imgui lifecycle reported " << validation_errors
                  << " validation error(s)\n";
        return 1;
    }
    std::cout << "sokol_imgui setup/frame/shutdown probe passed\n";
    return 0;
}
