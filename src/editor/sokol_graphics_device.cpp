#include "sokol_graphics_device.hpp"

#include "sokol_log.h"

namespace marrow::editor::shell {

SokolGraphicsDevice::~SokolGraphicsDevice() {
    shutdown();
}

std::optional<std::string> SokolGraphicsDevice::initialize(
    const sg_environment& environment) {
    if (sg_isvalid()) {
        return "Nested sg_setup() is forbidden: a Sokol graphics device is already active.";
    }
    sg_desc descriptor{};
    descriptor.environment = environment;
    descriptor.uniform_buffer_size = 1024 * 1024;
    descriptor.logger.func = slog_func;
    sg_setup(&descriptor);
    if (!sg_isvalid()) {
        return "Failed to initialize the editor Sokol graphics device.";
    }
    owns_device_ = true;
    return std::nullopt;
}

void SokolGraphicsDevice::shutdown() noexcept {
    if (owns_device_ && sg_isvalid()) {
        sg_shutdown();
    }
    owns_device_ = false;
}

bool SokolGraphicsDevice::valid() const noexcept {
    return owns_device_ && sg_isvalid();
}

} // namespace marrow::editor::shell
