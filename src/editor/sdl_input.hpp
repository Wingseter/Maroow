#pragma once

#include <optional>

#include <SDL3/SDL_events.h>

#include "windowing.hpp"

namespace marrow::editor::shell {

std::optional<PointerEvent> translate_sdl_pointer_event(
    const SDL_Event& event) noexcept;

} // namespace marrow::editor::shell
