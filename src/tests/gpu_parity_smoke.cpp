#include "gpu_readback.hpp"

#include "../editor/sokol_graphics_device.hpp"
#include "../editor/window_host.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 64;
constexpr std::array<std::uint8_t, 4> kExpectedRgba{{64U, 128U, 191U, 255U}};

bool pixel_matches(
    const std::vector<std::uint8_t>& rgba,
    int x,
    int y) {
    const std::size_t offset =
        (static_cast<std::size_t>(y) * kWidth + static_cast<std::size_t>(x)) * 4U;
    if (offset + 4U > rgba.size()) {
        return false;
    }
    for (std::size_t component = 0U; component < 4U; ++component) {
        const int difference = std::abs(
            static_cast<int>(rgba[offset + component]) -
            static_cast<int>(kExpectedRgba[component]));
        if (difference > 2) {
            return false;
        }
    }
    return true;
}

bool render_and_readback_once() {
    sg_image_desc image_desc{};
    image_desc.width = kWidth;
    image_desc.height = kHeight;
    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    image_desc.sample_count = 1;
    image_desc.usage.color_attachment = true;
    image_desc.usage.immutable = false;
    image_desc.label = "marrow-gpu-parity-image";
    const sg_image image = sg_make_image(&image_desc);
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
        std::cerr << "GPU parity image allocation failed\n";
        return false;
    }

    sg_view_desc view_desc{};
    view_desc.color_attachment.image = image;
    view_desc.label = "marrow-gpu-parity-view";
    const sg_view view = sg_make_view(&view_desc);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
        sg_destroy_image(image);
        std::cerr << "GPU parity attachment-view allocation failed\n";
        return false;
    }

    sg_pass pass{};
    pass.attachments.colors[0] = view;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = {0.25f, 0.5f, 0.75f, 1.0f};
    pass.label = "marrow-gpu-parity-pass";
    sg_begin_pass(&pass);
    sg_end_pass();
    sg_commit();

    const marrow::tests::GpuReadbackResult readback =
        marrow::tests::read_sokol_rgba8_image(image, kWidth, kHeight);
    const bool valid = readback &&
        pixel_matches(readback.top_left_rgba8, 0, 0) &&
        pixel_matches(readback.top_left_rgba8, kWidth / 2, kHeight / 2) &&
        pixel_matches(readback.top_left_rgba8, kWidth - 1, kHeight - 1);
    if (!valid) {
        std::cerr << (readback.error.empty()
            ? "GPU parity pixel probes exceeded the 2/255 tolerance"
            : readback.error) << '\n';
    }

    sg_destroy_view(view);
    sg_destroy_image(image);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_INVALID ||
        sg_query_image_state(image) != SG_RESOURCESTATE_INVALID) {
        std::cerr << "Destroyed GPU parity resources remained live\n";
        return false;
    }
    return valid;
}

} // namespace

int main() {
    using namespace marrow::editor::shell;
    for (int lifecycle = 0; lifecycle < 20; ++lifecycle) {
        auto host = create_sdl_window_host();
        WindowHostConfig config;
        config.logical_width = 320;
        config.logical_height = 240;
        config.title = "Marrow GPU Parity Smoke";
        config.visible = lifecycle == 0;
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

        SokolGraphicsDevice device;
        if (const auto error = device.initialize(host->graphics_environment())) {
            std::cerr << *error << '\n';
            host->shutdown();
            return 1;
        }

        for (int resource_cycle = 0; resource_cycle < 5; ++resource_cycle) {
            if (!render_and_readback_once()) {
                device.shutdown();
                host->shutdown();
                return 1;
            }
        }

        const FrameSurface surface = host->acquire_frame_surface();
        if (!surface.acquired) {
            std::cerr << "GPU parity smoke could not acquire the main swapchain\n";
            device.shutdown();
            host->shutdown();
            return 1;
        }
        sg_pass main_pass{};
        main_pass.swapchain = surface.swapchain;
        main_pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        main_pass.action.colors[0].store_action = SG_STOREACTION_STORE;
        main_pass.action.colors[0].clear_value = {0.05f, 0.06f, 0.08f, 1.0f};
        sg_begin_pass(&main_pass);
        sg_end_pass();
        sg_commit();
        host->present();

        device.shutdown();
        host->shutdown();
    }

    std::cout << "GPU parity smoke passed top-left RGBA8 probes, 20 device "
                 "lifecycles, and 100 resource lifecycles\n";
    return 0;
}
