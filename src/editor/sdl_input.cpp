#include "sdl_input.hpp"

#include <SDL3/SDL_pen.h>

namespace marrow::editor::shell {

namespace {

bool eraser_from_state(SDL_PenInputFlags state) noexcept {
    return (state & SDL_PEN_INPUT_ERASER_TIP) != 0U;
}

PointerEvent pen_position_event(
    PointerEventKind kind,
    SDL_PenID pen_id,
    SDL_WindowID window_id,
    SDL_PenInputFlags state,
    float x,
    float y,
    std::uint64_t timestamp) noexcept {
    PointerEvent translated;
    translated.kind = kind;
    translated.pen_id = pen_id;
    translated.window_id = window_id;
    translated.logical_x = x;
    translated.logical_y = y;
    translated.eraser = eraser_from_state(state);
    translated.timestamp = timestamp;
    return translated;
}

} // namespace

std::optional<PointerEvent> translate_sdl_pointer_event(
    const SDL_Event& event) noexcept {
    switch (event.type) {
    case SDL_EVENT_WINDOW_FOCUS_LOST: {
        PointerEvent translated;
        translated.kind = PointerEventKind::FocusLost;
        translated.window_id = event.window.windowID;
        translated.position_valid = false;
        translated.timestamp = event.window.timestamp;
        return translated;
    }
    case SDL_EVENT_PEN_PROXIMITY_IN:
    case SDL_EVENT_PEN_PROXIMITY_OUT: {
        PointerEvent translated;
        translated.kind = event.type == SDL_EVENT_PEN_PROXIMITY_IN
            ? PointerEventKind::PenProximityIn
            : PointerEventKind::PenProximityOut;
        translated.pen_id = event.pproximity.which;
        translated.window_id = event.pproximity.windowID;
        translated.position_valid = false;
        translated.timestamp = event.pproximity.timestamp;
        return translated;
    }
    case SDL_EVENT_PEN_DOWN:
    case SDL_EVENT_PEN_UP: {
        PointerEvent translated = pen_position_event(
            event.type == SDL_EVENT_PEN_DOWN
                ? PointerEventKind::PenDown
                : PointerEventKind::PenUp,
            event.ptouch.which,
            event.ptouch.windowID,
            event.ptouch.pen_state,
            event.ptouch.x,
            event.ptouch.y,
            event.ptouch.timestamp);
        translated.eraser = event.ptouch.eraser;
        return translated;
    }
    case SDL_EVENT_PEN_MOTION:
        return pen_position_event(
            PointerEventKind::PenMotion,
            event.pmotion.which,
            event.pmotion.windowID,
            event.pmotion.pen_state,
            event.pmotion.x,
            event.pmotion.y,
            event.pmotion.timestamp);
    case SDL_EVENT_PEN_AXIS: {
        PointerEvent translated = pen_position_event(
            PointerEventKind::PenAxis,
            event.paxis.which,
            event.paxis.windowID,
            event.paxis.pen_state,
            event.paxis.x,
            event.paxis.y,
            event.paxis.timestamp);
        switch (event.paxis.axis) {
        case SDL_PEN_AXIS_PRESSURE:
            translated.pressure = event.paxis.value;
            break;
        case SDL_PEN_AXIS_XTILT:
            translated.tilt_x = event.paxis.value;
            break;
        case SDL_PEN_AXIS_YTILT:
            translated.tilt_y = event.paxis.value;
            break;
        default:
            break;
        }
        return translated;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.which != SDL_PEN_MOUSEID) {
            PointerEvent translated;
            translated.kind = PointerEventKind::MouseActivity;
            translated.window_id = event.motion.windowID;
            translated.logical_x = event.motion.x;
            translated.logical_y = event.motion.y;
            translated.timestamp = event.motion.timestamp;
            return translated;
        }
        return std::nullopt;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.which != SDL_PEN_MOUSEID) {
            PointerEvent translated;
            translated.kind = PointerEventKind::MouseActivity;
            translated.window_id = event.button.windowID;
            translated.logical_x = event.button.x;
            translated.logical_y = event.button.y;
            translated.timestamp = event.button.timestamp;
            return translated;
        }
        return std::nullopt;
    default:
        return std::nullopt;
    }
}

} // namespace marrow::editor::shell
