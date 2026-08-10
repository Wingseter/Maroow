#pragma once

#include <cstdint>
#include <optional>

namespace marrow::editor::shell {

enum class RendererSurface {
    Metal,
    OpenGL,
};

struct WindowMetrics {
    int logical_width{0};
    int logical_height{0};
    int drawable_width{0};
    int drawable_height{0};
    float framebuffer_scale_x{1.0f};
    float framebuffer_scale_y{1.0f};
    float display_content_scale{1.0f};
    bool focused{false};
    bool minimized{false};
};

WindowMetrics make_window_metrics(
    int logical_width,
    int logical_height,
    int drawable_width,
    int drawable_height,
    float display_content_scale,
    bool focused,
    bool minimized);

bool drawable_size_changed(
    const WindowMetrics& before,
    const WindowMetrics& after) noexcept;

struct ViewportTextureUv {
    float u0{0.0f};
    float v0{0.0f};
    float u1{1.0f};
    float v1{1.0f};
};

ViewportTextureUv viewport_texture_uv(bool origin_top_left) noexcept;

enum class PointerSource {
    Mouse,
    Pen,
};

struct ViewportPointerState {
    PointerSource source{PointerSource::Mouse};
    std::uint64_t active_pen_id{0};
    std::uint32_t window_id{0};
    float logical_x{0.0f};
    float logical_y{0.0f};
    bool down{false};
    bool proximity{false};
    float pressure{1.0f};
    bool pressure_valid{false};
    float tilt_x{0.0f};
    float tilt_y{0.0f};
    bool eraser{false};
    std::uint64_t last_event_timestamp{0};

    float stroke_pressure() const noexcept;
};

enum class PointerEventKind {
    MouseActivity,
    PenProximityIn,
    PenProximityOut,
    PenDown,
    PenUp,
    PenMotion,
    PenAxis,
    FocusLost,
};

struct PointerEvent {
    PointerEventKind kind{PointerEventKind::MouseActivity};
    std::uint64_t pen_id{0};
    std::uint32_t window_id{0};
    float logical_x{0.0f};
    float logical_y{0.0f};
    bool position_valid{true};
    std::optional<float> pressure;
    std::optional<float> tilt_x;
    std::optional<float> tilt_y;
    bool eraser{false};
    std::uint64_t timestamp{0};
};

class PointerMediator {
public:
    bool process(const PointerEvent& event) noexcept;
    void reset() noexcept;

    const ViewportPointerState& state() const noexcept { return state_; }

private:
    bool accepts_pen(std::uint64_t pen_id) const noexcept;
    void update_pen_metadata(const PointerEvent& event) noexcept;

    ViewportPointerState state_{};
};

double pressure_scaled_strength(
    double configured_strength,
    double pressure,
    double radial_falloff) noexcept;

} // namespace marrow::editor::shell
