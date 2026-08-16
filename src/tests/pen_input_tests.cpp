#include "../editor/windowing.hpp"
#include "../editor/sdl_input.hpp"

#include <SDL3/SDL_pen.h>

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

marrow::editor::shell::PointerEvent pen_event(
    marrow::editor::shell::PointerEventKind kind,
    std::uint64_t id,
    std::optional<float> pressure = std::nullopt) {
    marrow::editor::shell::PointerEvent event;
    event.kind = kind;
    event.pen_id = id;
    event.window_id = 7;
    event.logical_x = 24.0f;
    event.logical_y = 32.0f;
    event.pressure = pressure;
    event.timestamp = 42;
    return event;
}

} // namespace

int main() {
    using namespace marrow::editor::shell;

    PointerMediator mediator;
    expect(mediator.state().stroke_pressure() == 1.0f,
           "ordinary mouse pressure must default to one");

    expect(mediator.process(pen_event(PointerEventKind::PenDown, 11, 0.5f)),
           "the first pen down must acquire stroke ownership");
    expect(std::abs(mediator.state().stroke_pressure() - 0.5f) < 1e-6f,
           "finite pressure must reach the active stroke");
    expect(!mediator.process(pen_event(PointerEventKind::PenAxis, 12, 1.0f)),
           "another pen id must not steal an active stroke");
    expect(std::abs(mediator.state().stroke_pressure() - 0.5f) < 1e-6f,
           "ignored pen events must preserve the owner pressure");

    expect(mediator.process(pen_event(PointerEventKind::PenAxis, 11, 2.0f)),
           "the owner pen axis must update metadata");
    expect(mediator.state().stroke_pressure() == 1.0f,
           "pressure must clamp to one");
    expect(mediator.process(pen_event(
               PointerEventKind::PenAxis,
               11,
               std::numeric_limits<float>::quiet_NaN())),
           "non-finite owner metadata may be observed without mutation");
    expect(mediator.state().stroke_pressure() == 1.0f,
           "non-finite pressure must retain the last valid value");

    expect(mediator.process(pen_event(PointerEventKind::PenUp, 11)),
           "the owner pen up must release the stroke");
    expect(mediator.state().stroke_pressure() == 1.0f,
           "released pointers must use mouse-equivalent pressure");
    expect(mediator.process(pen_event(PointerEventKind::PenDown, 22)),
           "a pressure-axis-free pen must still acquire the stroke");
    expect(mediator.state().stroke_pressure() == 1.0f,
           "a missing pressure axis must fall back to one");

    PointerEvent focus_lost;
    focus_lost.kind = PointerEventKind::FocusLost;
    focus_lost.timestamp = 99;
    mediator.process(focus_lost);
    expect(!mediator.state().down && !mediator.state().proximity &&
               mediator.state().active_pen_id == 0,
           "focus loss must cancel pointer ownership");

    expect(pressure_scaled_strength(0.8, 0.0, 0.5) == 0.0,
           "zero pressure must produce a no-op stamp");
    expect(std::abs(pressure_scaled_strength(0.8, 0.5, 0.5) - 0.2) < 1e-12,
           "stamp strength must multiply configuration, pressure, and falloff");
    expect(std::abs(pressure_scaled_strength(0.8, 1.0, 0.5) - 0.4) < 1e-12,
           "pressure response must be monotonic");

    SDL_Event axis_event{};
    axis_event.type = SDL_EVENT_PEN_AXIS;
    axis_event.paxis.which = 77;
    axis_event.paxis.windowID = 5;
    axis_event.paxis.pen_state =
        SDL_PEN_INPUT_DOWN | SDL_PEN_INPUT_ERASER_TIP;
    axis_event.paxis.x = 12.0f;
    axis_event.paxis.y = 13.0f;
    axis_event.paxis.axis = SDL_PEN_AXIS_PRESSURE;
    axis_event.paxis.value = 0.25f;
    const auto translated_axis = translate_sdl_pointer_event(axis_event);
    expect(translated_axis.has_value() &&
               translated_axis->kind == PointerEventKind::PenAxis &&
               translated_axis->pressure == 0.25f &&
               translated_axis->eraser,
           "SDL pressure events must translate to metadata without mouse injection");

    SDL_Event synthetic_mouse{};
    synthetic_mouse.type = SDL_EVENT_MOUSE_MOTION;
    synthetic_mouse.motion.which = SDL_PEN_MOUSEID;
    expect(!translate_sdl_pointer_event(synthetic_mouse).has_value(),
           "SDL synthetic pen mouse motion must not duplicate raw pen metadata");
    synthetic_mouse.motion.which = 1;
    expect(translate_sdl_pointer_event(synthetic_mouse).has_value(),
           "ordinary SDL mouse motion must preserve mouse pressure parity");

    if (failures == 0) {
        std::cout << "Pen input: 6 cases passed\n";
    }
    return failures == 0 ? 0 : 1;
}
