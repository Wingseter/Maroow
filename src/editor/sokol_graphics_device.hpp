#pragma once

#include <optional>
#include <string>

#include "sokol_gfx.h"

namespace marrow::editor::shell {

class SokolGraphicsDevice {
public:
    SokolGraphicsDevice() = default;
    SokolGraphicsDevice(const SokolGraphicsDevice&) = delete;
    SokolGraphicsDevice& operator=(const SokolGraphicsDevice&) = delete;
    ~SokolGraphicsDevice();

    std::optional<std::string> initialize(const sg_environment& environment);
    void shutdown() noexcept;
    bool valid() const noexcept;

private:
    bool owns_device_{false};
};

} // namespace marrow::editor::shell
