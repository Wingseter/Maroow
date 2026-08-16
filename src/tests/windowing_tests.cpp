#include "../editor/windowing.hpp"

#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    using namespace marrow::editor::shell;

    const WindowMetrics retina = make_window_metrics(
        1440, 900, 2880, 1800, 2.0f, true, false);
    expect(retina.logical_width == 1440 && retina.logical_height == 900,
           "logical dimensions must remain in window coordinates");
    expect(retina.drawable_width == 2880 && retina.drawable_height == 1800,
           "drawable dimensions must remain in physical pixels");
    expect(retina.framebuffer_scale_x == 2.0f &&
               retina.framebuffer_scale_y == 2.0f,
           "framebuffer scale must derive independently per axis");
    expect(retina.display_content_scale == 2.0f && retina.focused,
           "display content scale and focus must be retained");

    const WindowMetrics zero = make_window_metrics(
        1440, 900, 0, 0, 0.0f, false, false);
    expect(zero.minimized, "a zero-pixel surface must be treated as minimized");
    expect(zero.framebuffer_scale_x == 1.0f &&
               zero.framebuffer_scale_y == 1.0f,
           "zero drawable dimensions must never create a zero UI scale");
    expect(drawable_size_changed(retina, zero),
           "pixel-size changes must be detected independently of logical size");

    const WindowMetrics focus_only = make_window_metrics(
        1440, 900, 2880, 1800, 2.0f, false, false);
    expect(!drawable_size_changed(retina, focus_only),
           "focus-only changes must not recreate GPU targets");

    if (failures == 0) {
        std::cout << "Windowing: 4 cases passed\n";
    }
    return failures == 0 ? 0 : 1;
}
