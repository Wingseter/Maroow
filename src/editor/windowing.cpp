#include "windowing.hpp"

#include <algorithm>
#include <cmath>

namespace marrow::editor::shell {

namespace {

float positive_or_one(float value) noexcept {
    return std::isfinite(value) && value > 0.0f ? value : 1.0f;
}

void apply_optional_finite(
    const std::optional<float>& value,
    float minimum,
    float maximum,
    float* destination,
    bool* validity = nullptr) noexcept {
    if (!value.has_value() || !std::isfinite(*value)) {
        return;
    }
    *destination = std::clamp(*value, minimum, maximum);
    if (validity != nullptr) {
        *validity = true;
    }
}

} // namespace

WindowMetrics make_window_metrics(
    int logical_width,
    int logical_height,
    int drawable_width,
    int drawable_height,
    float display_content_scale,
    bool focused,
    bool minimized) {
    WindowMetrics metrics;
    metrics.logical_width = std::max(logical_width, 0);
    metrics.logical_height = std::max(logical_height, 0);
    metrics.drawable_width = std::max(drawable_width, 0);
    metrics.drawable_height = std::max(drawable_height, 0);
    metrics.framebuffer_scale_x = metrics.logical_width > 0
        ? positive_or_one(
              static_cast<float>(metrics.drawable_width) /
              static_cast<float>(metrics.logical_width))
        : 1.0f;
    metrics.framebuffer_scale_y = metrics.logical_height > 0
        ? positive_or_one(
              static_cast<float>(metrics.drawable_height) /
              static_cast<float>(metrics.logical_height))
        : 1.0f;
    metrics.display_content_scale = positive_or_one(display_content_scale);
    metrics.focused = focused;
    metrics.minimized = minimized ||
        metrics.drawable_width == 0 || metrics.drawable_height == 0;
    return metrics;
}

bool drawable_size_changed(
    const WindowMetrics& before,
    const WindowMetrics& after) noexcept {
    return before.drawable_width != after.drawable_width ||
        before.drawable_height != after.drawable_height;
}

ViewportTextureUv viewport_texture_uv(bool origin_top_left) noexcept {
    return origin_top_left
        ? ViewportTextureUv{0.0f, 0.0f, 1.0f, 1.0f}
        : ViewportTextureUv{0.0f, 1.0f, 1.0f, 0.0f};
}

float ViewportPointerState::stroke_pressure() const noexcept {
    if (source != PointerSource::Pen || !down) {
        return 1.0f;
    }
    return pressure_valid && std::isfinite(pressure)
        ? std::clamp(pressure, 0.0f, 1.0f)
        : 1.0f;
}

bool PointerMediator::accepts_pen(std::uint64_t pen_id) const noexcept {
    return !state_.down || state_.active_pen_id == 0 ||
        state_.active_pen_id == pen_id;
}

void PointerMediator::update_pen_metadata(const PointerEvent& event) noexcept {
    state_.source = PointerSource::Pen;
    state_.active_pen_id = event.pen_id;
    state_.window_id = event.window_id;
    if (event.position_valid) {
        state_.logical_x = event.logical_x;
        state_.logical_y = event.logical_y;
    }
    state_.eraser = event.eraser;
    state_.last_event_timestamp = event.timestamp;
    apply_optional_finite(
        event.pressure, 0.0f, 1.0f, &state_.pressure, &state_.pressure_valid);
    apply_optional_finite(event.tilt_x, -90.0f, 90.0f, &state_.tilt_x);
    apply_optional_finite(event.tilt_y, -90.0f, 90.0f, &state_.tilt_y);
}

bool PointerMediator::process(const PointerEvent& event) noexcept {
    if (event.kind == PointerEventKind::FocusLost) {
        reset();
        state_.last_event_timestamp = event.timestamp;
        return true;
    }

    if (event.kind == PointerEventKind::MouseActivity) {
        if (state_.down && state_.source == PointerSource::Pen) {
            return false;
        }
        state_.source = PointerSource::Mouse;
        state_.active_pen_id = 0;
        state_.window_id = event.window_id;
        state_.logical_x = event.logical_x;
        state_.logical_y = event.logical_y;
        state_.pressure = 1.0f;
        state_.pressure_valid = false;
        state_.eraser = false;
        state_.last_event_timestamp = event.timestamp;
        return true;
    }

    if (!accepts_pen(event.pen_id)) {
        return false;
    }

    switch (event.kind) {
    case PointerEventKind::PenProximityIn:
        update_pen_metadata(event);
        state_.proximity = true;
        return true;
    case PointerEventKind::PenProximityOut:
        update_pen_metadata(event);
        state_.down = false;
        state_.proximity = false;
        state_.active_pen_id = 0;
        return true;
    case PointerEventKind::PenDown:
        update_pen_metadata(event);
        state_.down = true;
        state_.proximity = true;
        if (!event.pressure.has_value()) {
            state_.pressure = 1.0f;
            state_.pressure_valid = false;
        }
        return true;
    case PointerEventKind::PenUp:
        update_pen_metadata(event);
        state_.down = false;
        return true;
    case PointerEventKind::PenMotion:
    case PointerEventKind::PenAxis:
        update_pen_metadata(event);
        state_.proximity = true;
        return true;
    case PointerEventKind::MouseActivity:
    case PointerEventKind::FocusLost:
        break;
    }
    return false;
}

void PointerMediator::reset() noexcept {
    state_ = {};
}

double pressure_scaled_strength(
    double configured_strength,
    double pressure,
    double radial_falloff) noexcept {
    if (!std::isfinite(configured_strength) ||
        !std::isfinite(pressure) ||
        !std::isfinite(radial_falloff)) {
        return 0.0;
    }
    return std::clamp(configured_strength, 0.0, 1.0) *
        std::clamp(pressure, 0.0, 1.0) *
        std::clamp(radial_falloff, 0.0, 1.0);
}

} // namespace marrow::editor::shell
