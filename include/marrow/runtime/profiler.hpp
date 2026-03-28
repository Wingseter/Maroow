#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::runtime {

enum class ProfilerPhase {
    Animation,
    Transform,
    Constraints,
    Skinning,
    Render,
};

struct ProfilerBatchBreakReasons {
    std::size_t texture_changes{0};
    std::size_t blend_changes{0};
    std::size_t clip_changes{0};
    std::size_t shader_changes{0};
};

struct ProfilerDrawStats {
    std::size_t skeleton_count{0};
    std::size_t draw_calls{0};
    std::size_t vertices{0};
    std::size_t batch_merges{0};
    ProfilerBatchBreakReasons break_reasons{};
};

struct ProfilerFrame {
    std::uint64_t animation_us{0};
    std::uint64_t transform_us{0};
    std::uint64_t constraint_us{0};
    std::uint64_t skinning_us{0};
    std::uint64_t render_us{0};
    std::uint64_t total_us{0};
    std::size_t skeleton_count{0};
    std::size_t draw_calls{0};
    std::size_t vertices{0};
    std::size_t batch_merges{0};
    ProfilerBatchBreakReasons batch_break_reasons{};
};

class ProfilerCapture {
public:
    /**
     * @brief Creates a profiler capture object.
     * @param enabled Whether profiling collection should be active.
     */
    explicit ProfilerCapture(bool enabled = true);

    /// @brief Reports whether profiling is enabled.
    /// @return `true` when timing capture is active; otherwise `false`.
    bool enabled() const;
    /// @brief Starts a new frame capture.
    void begin_frame();
    /// @brief Finishes the current frame capture and seals the totals.
    void end_frame();
    /**
     * @brief Adds microseconds to one profiler phase bucket.
     * @param phase Phase bucket to accumulate.
     * @param microseconds Microseconds to add.
     */
    void add_phase_microseconds(ProfilerPhase phase, std::uint64_t microseconds);
    /**
     * @brief Adds world-transform and constraint timing totals.
     * @param timing_breakdown Timing data produced during pose evaluation.
     */
    void add_world_transform_timing(const WorldTransformTimingBreakdown& timing_breakdown);
    /**
     * @brief Adds draw and batching statistics to the current frame.
     * @param draw_stats Renderer-facing statistics for the frame.
     */
    void add_draw_stats(const ProfilerDrawStats& draw_stats);

private:
    using Clock = std::chrono::steady_clock;

    bool enabled_{true};
    bool frame_started_{false};
    Clock::time_point frame_start_{};
    ProfilerFrame frame_{};

    friend ProfilerFrame marrow_profiler_frame(const ProfilerCapture& capture);
};

/**
 * @brief Extracts the most recently captured profiler frame snapshot.
 * @param capture Profiler capture to inspect.
 * @return The current profiler frame snapshot.
 */
ProfilerFrame marrow_profiler_frame(const ProfilerCapture& capture);
/**
 * @brief Formats profiler metrics as HUD-ready text lines.
 * @param frame Profiler frame snapshot to summarize.
 * @return HUD lines suitable for debug overlays.
 */
std::vector<std::string> profiler_hud_lines(const ProfilerFrame& frame);

/**
 * @brief Measures a callable and records its elapsed time in one profiler phase.
 * @param capture Optional profiler capture receiving the timing.
 * @param phase Phase bucket to accumulate.
 * @param fn Callable to execute.
 * @return Whatever value `fn()` returns.
 */
template <typename Fn>
auto profile_phase(ProfilerCapture* capture, ProfilerPhase phase, Fn&& fn)
    -> decltype(fn()) {
    if (capture == nullptr || !capture->enabled()) {
        return fn();
    }

    const auto start = std::chrono::steady_clock::now();
    if constexpr (std::is_void_v<decltype(fn())>) {
        fn();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        capture->add_phase_microseconds(
            phase,
            static_cast<std::uint64_t>(elapsed < 0 ? 0 : elapsed));
    } else {
        auto result = fn();
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        capture->add_phase_microseconds(
            phase,
            static_cast<std::uint64_t>(elapsed < 0 ? 0 : elapsed));
        return result;
    }
}

} // namespace marrow::runtime
